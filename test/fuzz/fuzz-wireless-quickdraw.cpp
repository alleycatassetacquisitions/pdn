#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "device/drivers/native/native-prefs-driver.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "fuzz-common.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "id-generator.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/match-packet.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"

namespace {

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

    void clearPacketHandler(PktType type) override { rxHandlers.erase(type); }

    void setSendStatusHandler(PktType type, SendStatusCallback callback, void* ctx) override {
        txStatusHandlers[type] = SendStatusEntry{callback, ctx};
    }

    void clearSendStatusHandler(PktType type) override { txStatusHandlers.erase(type); }

    // Exact-sized heap buffers so ASan catches reads past the 6-byte MAC or len-byte payload.
    void deliverRxExact(PktType type, const uint8_t* srcMac6, const uint8_t* payload, size_t len) {
        auto it = rxHandlers.find(type);
        if (it == rxHandlers.end() || it->second.callback == nullptr) {
            return;
        }
        ExactBuffer exactMac(srcMac6, 6);
        ExactBuffer exactPayload(payload, len);
        it->second.callback(exactMac.data(), exactPayload.data(), len, it->second.ctx);
    }

    void deliverTxStatusExact(PktType type, const uint8_t* dstMac6, const uint8_t* payload,
                              size_t len, bool success) {
        auto it = txStatusHandlers.find(type);
        if (it == txStatusHandlers.end() || it->second.callback == nullptr) {
            return;
        }
        ExactBuffer exactMac(dstMac6, 6);
        ExactBuffer exactPayload(payload, len);
        it->second.callback(exactMac.data(), exactPayload.data(), len, success, it->second.ctx);
    }

    void ackLastSent(bool success) {
        if (!lastSentBytes.empty()) {
            deliverTxStatusExact(lastSentType, lastSentDst, lastSentBytes.data(),
                                 lastSentBytes.size(), success);
        }
    }

    const uint8_t* getGlobalBroadcastAddress() override { return broadcastMac; }

    uint8_t* getMacAddress() override { return localMac; }

    void setPeerCommsState(PeerCommsState) override {}

    PeerCommsState getPeerCommsState() override { return PeerCommsState::ENABLED; }

    void enable() override {}
    void disable() override {}

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
    PktType lastSentType = PktType::kMatchCommand;
    uint8_t lastSentDst[6] = {};
    std::vector<uint8_t> lastSentBytes;
};

class FuzzRemoteDeviceCoordinator : public RemoteDeviceCoordinator {
public:
    const uint8_t* getPeerMac(SerialIdentifier jack) const override {
        if (jack == SerialIdentifier::INPUT_JACK) {
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
    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static bool idGenInitialized = false;
    if (!idGenInitialized) {
        IdGenerator::initialize(42);
        idGenInitialized = true;
    }

    FuzzStepClock clock;
    SimpleTimer::setPlatformClock(&clock);

    FuzzPeerComms peerComms;
    NativePrefsDriver storage("fuzz", {MATCHES_PREFS_NAMESPACE});
    FuzzRemoteDeviceCoordinator rdc;

    WirelessManager wirelessManager(&peerComms, nullptr);
    MatchManager matchManager(&wirelessManager);
    SymbolWirelessManager symbolWirelessManager(&wirelessManager, &rdc);
    RemotePlayerManager remotePlayerManager(&peerComms);

    Player player;
    char initialUserId[] = "0042";
    player.setUserID(initialUserId);
    player.setIsHunter((size > 0) ? ((data[0] & 1U) != 0U) : true);

    matchManager.initialize(&player, &storage);
    matchManager.setRemoteDeviceCoordinator(&rdc);
    remotePlayerManager.StartBroadcastingPlayerInfo(&player, 5000);
    wirelessManager.setEspNowPacketHandler(
        PktType::kPlayerInfoBroadcast,
        [](const uint8_t* srcMac, const uint8_t* pktData, const size_t pktLen, void* ctx) {
            static_cast<RemotePlayerManager*>(ctx)->ProcessPlayerInfoPkt(srcMac, pktData, pktLen);
        },
        &remotePlayerManager);

    symbolWirelessManager.setPacketReceivedCallback(
        [](const SymbolMatchCommand& cmd) {
            volatile int sink = cmd.command ^ static_cast<int>(cmd.symbolId);
            (void)sink;
        },
        SerialIdentifier::INPUT_JACK);

    uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    symbolWirelessManager.setMacPeer(peerMac);

    size_t pos = 0;
    while (pos < size) {
        uint8_t op = data[pos++] & 0x0FU;

        switch (op) {
            case 0: {
                // Variable-length raw frame to exercise ReliableChannel::deliver length checks.
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
                peerComms.deliverRxExact(PktType::kMatchCommand, srcMac, data + pos, pktLen);
                pos += pktLen;
                break;
            }
            case 1: {
                // Deliver a raw MatchPacket directly from fuzz bytes (tests non-null-terminated
                // matchId and arbitrary command/drawTime values)
                if (size - pos < sizeof(MatchPacket)) {
                    pos = size;
                    break;
                }
                peerComms.deliverRxExact(PktType::kMatchCommand, peerMac, data + pos,
                                         sizeof(MatchPacket));
                pos += sizeof(MatchPacket);
                break;
            }
            case 2: {
                if (size - pos < 3) {
                    pos = size;
                    break;
                }
                MatchPacket pkt{};
                if (matchManager.getCurrentMatch().has_value()) {
                    std::memcpy(pkt.matchId, matchManager.getCurrentMatch()->getMatchId(),
                                IdGenerator::UUID_BUFFER_SIZE);
                } else {
                    std::memset(pkt.matchId, 'A', IdGenerator::UUID_BUFFER_SIZE);
                }
                pkt.isHunter = data[pos];
                pkt.command = static_cast<int>(data[pos + 1] % 12U);
                pkt.seqId = data[pos + 2];
                pkt.playerDrawTime = static_cast<long>(static_cast<int8_t>(data[pos])) * 100L;
                pos += 3;
                peerComms.deliverRxExact(PktType::kMatchCommand, peerMac,
                                         reinterpret_cast<const uint8_t*>(&pkt),
                                         sizeof(MatchPacket));
                break;
            }
            case 3: {
                matchManager.initializeMatch(peerMac);
                matchManager.setMatchLocalStartTime(clock.milliseconds());
                break;
            }
            case 4: {
                matchManager.initializeShootoutMatch("SHT-0001000200030004000500060007", peerMac,
                                                     player.isHunter());
                matchManager.setMatchLocalStartTime(clock.milliseconds());
                break;
            }
            case 5: {
                parameterizedCallbackFunction push = matchManager.getMatchButtonPush();
                if (push != nullptr) {
                    push(&matchManager);
                }
                break;
            }
            case 6: {
                parameterizedCallbackFunction mash = matchManager.getButtonMasher();
                if (mash != nullptr) {
                    mash(&matchManager);
                }
                break;
            }
            case 7: {
                matchManager.sendNeverPressed(1500);
                (void)matchManager.matchResultsAreIn();
                (void)matchManager.didWin();
                break;
            }
            case 8: {
                if (pos < size) {
                    uint32_t seqOverride = data[pos++];
                    if (seqOverride >= 30) {
                        storage.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, seqOverride);
                    }
                }
                if (matchManager.finalizeMatch()) {
                    Match stored{};
                    volatile bool ok =
                    matchManager.readMatchBySequence(matchManager.getMatchSequenceNumber(), stored);
                    (void)ok;
                }
                break;
            }
            case 9: {
                if (pos < size) {
                    unsigned long delta = static_cast<unsigned long>(data[pos++]) * 25UL;
                    clock.advance(delta);
                }
                wirelessManager.sync();
                remotePlayerManager.Update();
                break;
            }
            case 10: {
                bool success = (pos < size) ? ((data[pos++] & 1U) != 0U) : true;
                peerComms.ackLastSent(success);
                break;
            }
            case 11: {
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
                if (pos < size) {
                    uint8_t b = data[pos++];
                    symbolWirelessManager.sendPacket(static_cast<int>(b & 0x07U),
                                                     static_cast<SymbolId>((b >> 3U) % 8U),
                                                     SerialIdentifier::INPUT_JACK);
                }
                break;
            }
            case 13: {
                // Burst 34 sender MACs to force ReliableChannelBase::rxSeq eviction past MAX_RX_SENDERS (32).
                MatchPacket pkt{};
                uint8_t selector = (pos < size) ? data[pos++] : 0U;
                pkt.command = static_cast<int>(MatchCmd::SEND_MATCH_ID + (selector % 5U));
                pkt.isHunter = selector & 1U;
                for (uint8_t i = 1; i <= 34; ++i) {
                    uint8_t burstMac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, i};
                    pkt.seqId = i;
                    peerComms.deliverRxExact(PktType::kMatchCommand, burstMac,
                                             reinterpret_cast<const uint8_t*>(&pkt),
                                             sizeof(MatchPacket));
                }
                break;
            }
            case 14: {
                if (size - pos < 2) {
                    pos = size;
                    break;
                }
                uint8_t senderId = data[pos++];
                uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, senderId};
                size_t pktLen = data[pos++] % 64U;
                if (pktLen > size - pos) {
                    pktLen = size - pos;
                }
                peerComms.deliverRxExact(PktType::kPlayerInfoBroadcast, srcMac, data + pos, pktLen);
                pos += pktLen;
                break;
            }
            default: {
                player.setIsHunter(!player.isHunter());
                matchManager.clearCurrentMatch();
                break;
            }
        }

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
            assertMatchStringBounds(*matchManager.getCurrentMatch());
        }
    }

    SimpleTimer::setPlatformClock(nullptr);
    return 0;
}
