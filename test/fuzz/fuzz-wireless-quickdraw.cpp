#include "utils/UUID.h"

#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

// Expose IdGenerator's internal UUID generator to the fuzz harness so each
// persistent-mode iteration resets the PRNG state deterministically (Tip #4)
// without std::random_device entropy causing path instability across runs.
#define private public
#include "id-generator.hpp"
#undef private

#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/platform-clock.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/quickdraw-packet.hpp"
#include "wireless/symbol-wireless-manager.hpp"

namespace {

class FuzzNullLogger : public LoggerInterface {
public:
    void vlog(LogLevel, const char*, const char*, int, const char*, va_list) override {}
};

class FuzzStepClock : public PlatformClock {
public:
    unsigned long milliseconds() override {
        return currentTimeMs;
    }

    void setTime(unsigned long timeMs) {
        currentTimeMs = timeMs;
    }

    void advance(unsigned long deltaMs) {
        currentTimeMs += deltaMs;
    }

private:
    unsigned long currentTimeMs = 10000;
};

class FuzzPeerComms : public PeerCommsInterface {
public:
    int sendData(const uint8_t* dst, PktType type, const uint8_t* data, const size_t len) override {
        if (dst != nullptr && data != nullptr && len > 0) {
            lastSentType = type;
            std::memcpy(lastSentDst, dst, 6);
            lastSentBytes.assign(data, data + len);
        }
        return 1;
    }

    void setPacketHandler(PktType type, PacketCallback callback, void* ctx) override {
        rxHandlers[type] = HandlerEntry{callback, ctx};
    }

    void clearPacketHandler(PktType type) override {
        rxHandlers.erase(type);
    }

    void setSendStatusHandler(PktType type, SendStatusCallback callback, void* ctx) override {
        txStatusHandlers[type] = SendStatusEntry{callback, ctx};
    }

    void clearSendStatusHandler(PktType type) override {
        txStatusHandlers.erase(type);
    }

    // Copy both the 6-byte MAC and the payload into exact-sized heap allocations
    // (Tip #5) so ASan catches any out-of-bounds read past 6 or `len` bytes.
    void deliverRxExact(PktType type, const uint8_t* srcMac6, const uint8_t* payload, size_t len) {
        auto it = rxHandlers.find(type);
        if (it == rxHandlers.end() || it->second.callback == nullptr) {
            return;
        }
        auto* exactMac = new uint8_t[6];
        std::memcpy(exactMac, srcMac6, 6);

        auto* exactPayload = new uint8_t[len];
        if (len > 0) {
            std::memcpy(exactPayload, payload, len);
        }

        it->second.callback(exactMac, exactPayload, len, it->second.ctx);

        delete[] exactPayload;
        delete[] exactMac;
    }

    void deliverTxStatusExact(PktType type, const uint8_t* dstMac6, const uint8_t* payload,
                              size_t len, bool success) {
        auto it = txStatusHandlers.find(type);
        if (it == txStatusHandlers.end() || it->second.callback == nullptr) {
            return;
        }
        auto* exactMac = new uint8_t[6];
        std::memcpy(exactMac, dstMac6, 6);

        auto* exactPayload = new uint8_t[len];
        if (len > 0) {
            std::memcpy(exactPayload, payload, len);
        }

        it->second.callback(exactMac, exactPayload, len, success, it->second.ctx);

        delete[] exactPayload;
        delete[] exactMac;
    }

    void ackLastSent(bool success) {
        if (!lastSentBytes.empty()) {
            deliverTxStatusExact(lastSentType, lastSentDst, lastSentBytes.data(),
                                 lastSentBytes.size(), success);
        }
    }

    const uint8_t* getGlobalBroadcastAddress() override {
        return broadcastMac;
    }

    uint8_t* getMacAddress() override {
        return localMac;
    }

    void setPeerCommsState(PeerCommsState) override {}

    PeerCommsState getPeerCommsState() override {
        return PeerCommsState::CONNECTED;
    }

    void connect() override {}
    void disconnect() override {}

private:
    struct HandlerEntry {
        PacketCallback callback = nullptr;
        void* ctx = nullptr;
    };

    struct SendStatusEntry {
        SendStatusCallback callback = nullptr;
        void* ctx = nullptr;
    };

    std::map<PktType, HandlerEntry> rxHandlers;
    std::map<PktType, SendStatusEntry> txStatusHandlers;
    uint8_t localMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    PktType lastSentType = PktType::kQuickdrawCommand;
    uint8_t lastSentDst[6] = {};
    std::vector<uint8_t> lastSentBytes;
};

class FuzzStorage : public StorageInterface {
public:
    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        if (stringStore.size() > 64) {
            stringStore.clear();
        }
        stringStore[ns + ":" + key] = value;
        return value.size();
    }

    std::string read(const std::string& ns, const std::string& key,
                     const std::string& defaultValue) override {
        auto it = stringStore.find(ns + ":" + key);
        if (it != stringStore.end()) {
            return it->second;
        }
        return defaultValue;
    }

    bool remove(const std::string& ns, const std::string& key) override {
        stringStore.erase(ns + ":" + key);
        ucharStore.erase(ns + ":" + key);
        return true;
    }

    bool clear(const std::string&) override {
        stringStore.clear();
        ucharStore.clear();
        return true;
    }

    void end() override {}

    uint8_t readUChar(const std::string& ns, const std::string& key,
                      uint8_t defaultValue) override {
        auto it = ucharStore.find(ns + ":" + key);
        if (it != ucharStore.end()) {
            return it->second;
        }
        return defaultValue;
    }

    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        ucharStore[ns + ":" + key] = value;
        return 1;
    }

private:
    std::map<std::string, std::string> stringStore;
    std::map<std::string, uint8_t> ucharStore;
};

class FuzzHttpClient : public HttpClientInterface {
public:
    void setWifiConfig(WifiConfig*) override {}
    bool isConnected() override {
        return false;
    }
    bool queueRequest(HttpRequest&) override {
        return false;
    }
    void disconnect() override {}
    void updateConfig(WifiConfig*) override {}
    void retryConnection() override {}
    uint8_t* getMacAddress() override {
        return macAddress;
    }
    void setHttpClientState(HttpClientState) override {}
    HttpClientState getHttpClientState() override {
        return HttpClientState::DISCONNECTED;
    }

private:
    uint8_t macAddress[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
};

class FuzzRemoteDeviceCoordinator : public RemoteDeviceCoordinator {
public:
    FuzzRemoteDeviceCoordinator() {
        const uint8_t defaultPeer[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        std::memcpy(peerMac, defaultPeer, 6);
    }

    void setDirectPeer(const uint8_t* mac) {
        std::memcpy(peerMac, mac, 6);
    }

    const uint8_t* getPeerMac(SerialIdentifier port) const override {
        if (port == SerialIdentifier::INPUT_JACK) {
            return peerMac;
        }
        return nullptr;
    }

    bool isDirectPeer(const uint8_t* mac) const override {
        if (mac == nullptr) {
            return false;
        }
        return std::memcmp(mac, peerMac, 6) == 0;
    }

private:
    uint8_t peerMac[6] = {};
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static FuzzNullLogger nullLogger;
    g_logger = &nullLogger;

    static bool idGenInitialized = false;
    if (!idGenInitialized) {
        IdGenerator::initialize(42);
        idGenInitialized = true;
    }
    // Deterministically reset the Marsaglia PRNG state before every testcase (Tip #4).
    IdGenerator::getInstance().generator.seed(42, 42);
    IdGenerator::getInstance().generator.setVersion4Mode();

    FuzzStepClock clock;
    SimpleTimer::setPlatformClock(&clock);

    FuzzPeerComms peerComms;
    FuzzStorage storage;
    FuzzHttpClient httpClient;
    FuzzRemoteDeviceCoordinator rdc;

    WirelessManager wirelessManager(&peerComms, &httpClient);
    MatchManager matchManager(&wirelessManager);
    SymbolWirelessManager symbolWirelessManager(&wirelessManager, &rdc);

    Player player;
    char initialUserId[] = "0042";
    player.setUserID(initialUserId);
    player.setIsHunter((size > 0) ? ((data[0] & 1U) != 0U) : true);

    matchManager.initialize(&player, &storage);
    matchManager.setRemoteDeviceCoordinator(&rdc);

    symbolWirelessManager.setPacketReceivedCallback(
        [](const SymbolMatchCommand& cmd) {
            volatile int sink = cmd.command ^ static_cast<int>(cmd.symbolId);
            (void)sink;
        },
        SerialIdentifier::INPUT_JACK);

    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    rdc.setDirectPeer(peerMac);
    symbolWirelessManager.setMacPeer(peerMac);

    size_t pos = 0;
    while (pos < size) {
        uint8_t op = data[pos++] & 0x0FU;

        switch (op) {
            case 0: {
                // Deliver arbitrary raw bytes to kQuickdrawCommand from a compact 1-byte sender ID
                // (exercises length mismatch checks and rxSeq deduplication table eviction).
                if (size - pos < 2) {
                    pos = size;
                    break;
                }
                uint8_t senderId = data[pos++];
                uint8_t srcMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, senderId};
                size_t pktLen = data[pos++] % 96U;
                if (pktLen > size - pos) {
                    pktLen = size - pos;
                }
                peerComms.deliverRxExact(PktType::kQuickdrawCommand, srcMac, data + pos, pktLen);
                pos += pktLen;
                break;
            }
            case 1: {
                // Deliver a raw QuickdrawPacket directly from fuzz bytes (tests non-null-terminated
                // matchId/playerId and arbitrary command/drawTime values)
                if (size - pos < sizeof(QuickdrawPacket)) {
                    pos = size;
                    break;
                }
                QuickdrawPacket pkt{};
                std::memcpy(&pkt, data + pos, sizeof(QuickdrawPacket));
                pos += sizeof(QuickdrawPacket);
                peerComms.deliverRxExact(PktType::kQuickdrawCommand, peerMac,
                                         reinterpret_cast<const uint8_t*>(&pkt),
                                         sizeof(QuickdrawPacket));
                break;
            }
            case 2: {
                // Deliver a state-aware QuickdrawPacket matching active matchId & opponentMac
                if (size - pos < 8) {
                    pos = size;
                    break;
                }
                QuickdrawPacket pkt{};
                if (matchManager.getCurrentMatch().has_value()) {
                    std::memcpy(pkt.matchId, matchManager.getCurrentMatch()->getMatchId(),
                                IdGenerator::UUID_BUFFER_SIZE);
                } else {
                    std::memset(pkt.matchId, 'A', IdGenerator::UUID_BUFFER_SIZE);
                }
                std::memcpy(pkt.playerId, data + pos, 5);
                pkt.isHunter = data[pos + 5];
                pkt.command = static_cast<int>(data[pos + 6] % 12U);
                pkt.seqId = data[pos + 7];
                pkt.playerDrawTime = static_cast<long>(static_cast<int8_t>(data[pos + 5])) * 100L;
                pos += 8;
                peerComms.deliverRxExact(PktType::kQuickdrawCommand, peerMac,
                                         reinterpret_cast<const uint8_t*>(&pkt),
                                         sizeof(QuickdrawPacket));
                break;
            }
            case 3: {
                // Initialize match as hunter/bounty
                matchManager.initializeMatch(peerMac);
                matchManager.setDuelLocalStartTime(clock.milliseconds());
                break;
            }
            case 4: {
                // Initialize shootout match
                matchManager.initializeShootoutMatch("SHT-0001000200030004000500060007", peerMac,
                                                     player.isHunter());
                matchManager.setDuelLocalStartTime(clock.milliseconds());
                break;
            }
            case 5: {
                // Press duel button
                parameterizedCallbackFunction push = matchManager.getDuelButtonPush();
                if (push != nullptr) {
                    push(&matchManager);
                }
                break;
            }
            case 6: {
                // Press button masher
                parameterizedCallbackFunction mash = matchManager.getButtonMasher();
                if (mash != nullptr) {
                    mash(&matchManager);
                }
                break;
            }
            case 7: {
                // Send never pressed & check results
                matchManager.sendNeverPressed(1500);
                (void)matchManager.matchResultsAreIn();
                (void)matchManager.didWin();
                break;
            }
            case 8: {
                // Optionally jump stored match count near MAX_MATCHES (255) boundary (Tip #8),
                // then finalize match & serialize stored matches to JSON
                if (pos < size) {
                    uint8_t countOverride = data[pos++];
                    if (countOverride >= 250) {
                        storage.writeUChar(MATCHES_PREFS_NAMESPACE, "count", countOverride);
                    }
                }
                if (matchManager.finalizeMatch()) {
                    std::string json = matchManager.toJson();
                    volatile size_t jsonLen = json.size();
                    (void)jsonLen;
                }
                break;
            }
            case 9: {
                // Advance clock and sync resenders
                if (pos < size) {
                    unsigned long delta = static_cast<unsigned long>(data[pos++]) * 25UL;
                    clock.advance(delta);
                }
                matchManager.sync();
                symbolWirelessManager.sync();
                break;
            }
            case 10: {
                // Deliver TX status (ack / fail) for last sent packet
                bool success = (pos < size) ? ((data[pos++] & 1U) != 0U) : true;
                peerComms.ackLastSent(success);
                break;
            }
            case 11: {
                // Deliver SymbolMatchPacket (raw fuzz bytes)
                if (size - pos < sizeof(SymbolMatchPacket)) {
                    pos = size;
                    break;
                }
                peerComms.deliverRxExact(PktType::kSymbolMatchCommand, peerMac, data + pos,
                                         sizeof(SymbolMatchPacket));
                pos += sizeof(SymbolMatchPacket);
                break;
            }
            case 12: {
                // Send symbol packet
                if (pos < size) {
                    uint8_t b = data[pos++];
                    symbolWirelessManager.sendPacket(static_cast<int>(b & 0x07U),
                                                     static_cast<SymbolId>((b >> 3U) % 8U),
                                                     SerialIdentifier::INPUT_JACK);
                }
                break;
            }
            case 13: {
                // Burst 34 distinct sender MACs to force ReliableChannelBase::rxSeq
                // eviction past MAX_RX_SENDERS (32) (Tip #8).
                QuickdrawPacket pkt{};
                pkt.command = QDCommand::DRAW_RESULT;
                for (uint8_t i = 1; i <= 34; ++i) {
                    uint8_t burstMac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, i};
                    pkt.seqId = i;
                    peerComms.deliverRxExact(PktType::kQuickdrawCommand, burstMac,
                                             reinterpret_cast<const uint8_t*>(&pkt),
                                             sizeof(QuickdrawPacket));
                }
                break;
            }
            default: {
                // Toggle player role or clear match
                player.setIsHunter(!player.isHunter());
                matchManager.clearCurrentMatch();
                break;
            }
        }

        // State-machine invariant oracle (Life Pro Tip #17):
        // Verify MatchManager state consistency and string bounds after every action.
        const bool hasActiveMatch = matchManager.getCurrentMatch().has_value();
        if (matchManager.isMatchReady() && !hasActiveMatch) {
            __builtin_trap();
        }
        if (matchManager.matchResultsAreIn() && !hasActiveMatch) {
            __builtin_trap();
        }
        if (matchManager.didWin() && !hasActiveMatch) {
            __builtin_trap();
        }
        if (hasActiveMatch) {
            const Match& currentMatch = *matchManager.getCurrentMatch();
            if (std::strlen(currentMatch.getMatchId()) >= IdGenerator::UUID_BUFFER_SIZE) {
                __builtin_trap();
            }
            if (std::strlen(currentMatch.getHunterId()) > PLAYER_ID_BINARY_SIZE) {
                __builtin_trap();
            }
            if (std::strlen(currentMatch.getBountyId()) > PLAYER_ID_BINARY_SIZE) {
                __builtin_trap();
            }
        }
    }

    SimpleTimer::setPlatformClock(nullptr);
    return 0;
}
