#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>
#include "alleycat-server/matches-api.hpp"
#include "alleycat-server/quickdraw-match-codec.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "game/match.hpp"

inline constexpr const char FDN_MATCH_SPOOL_NAMESPACE[] = "fdn_spool";
inline constexpr const char SPOOL_HEAD_KEY[] = "spool_head";
inline constexpr const char SPOOL_TAIL_KEY[] = "spool_tail";
inline constexpr const char SPOOL_SLOT_PREFIX[] = "spool_";
inline constexpr size_t FDN_MATCH_SPOOL_CAPACITY = 64;

/**
 * FDN persistent 64-slot match spool and batch uploader.
 *
 * Accepts MatchTransferPacket frames over PktType::kMatchCollection only from a
 * PDN that is Connected on an FDN serial jack, persists up to 64 records in NVS,
 * withholds MatchTransferAckPacket when full, and uploads staged batches via
 * POST /quickdraw-matches.
 */
class FdnMatchSpool {
public:
    /** Constructs the FDN match spool and registers the kMatchCollection handler. */
    FdnMatchSpool(WirelessManager* wirelessManager, StorageInterface* storage,
                  RemoteDeviceCoordinator* rdc = nullptr)
        : wirelessManager(wirelessManager)
        , storage(storage)
        , rdc(rdc) {
        if (this->wirelessManager) {
            this->wirelessManager->setEspNowPacketHandler(
            PktType::kMatchCollection,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                static_cast<FdnMatchSpool*>(ctx)->onMatchCollectionPacket(fromMac, data, dataLen);
            },
            this);
        }
    }

    /** Clears the kMatchCollection packet handler on destruction. */
    ~FdnMatchSpool() {
        if (wirelessManager) {
            wirelessManager->clearEspNowPacketHandler(PktType::kMatchCollection);
        }
    }

    /** Injects the RemoteDeviceCoordinator used to verify Connected PDN jacks. */
    void setRemoteDeviceCoordinator(RemoteDeviceCoordinator* coordinator) { rdc = coordinator; }

    /** @return Monotonic head sequence index of the spool (clamped against corruption). */
    uint32_t getHead() const {
        if (!storage) return 0;
        const uint32_t tail = storage->readUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_TAIL_KEY, 0);
        uint32_t head = storage->readUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY, 0);
        if (head > tail) {
            head = tail;
            storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY, head);
        } else if (tail - head > FDN_MATCH_SPOOL_CAPACITY) {
            head = tail - static_cast<uint32_t>(FDN_MATCH_SPOOL_CAPACITY);
            storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY, head);
        }
        return head;
    }

    /** @return Monotonic tail sequence index of the spool. */
    uint32_t getTail() const {
        if (!storage) return 0;
        return storage->readUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_TAIL_KEY, 0);
    }

    /** @return Number of staged matches awaiting upload (0..64). */
    size_t getStagedCount() const {
        const uint32_t head = getHead();
        const uint32_t tail = getTail();
        return (tail >= head) ? static_cast<size_t>(tail - head) : 0;
    }

    /** @return true when all 64 spool slots are occupied by unuploaded matches. */
    bool isFull() const { return getStagedCount() >= FDN_MATCH_SPOOL_CAPACITY; }

    /** Reads the staged match at @p offset relative to `getHead()`. */
    bool readStagedMatch(size_t offset, Match& out) const {
        const size_t count = getStagedCount();
        if (!storage || offset >= count) return false;
        const uint32_t index = getHead() + static_cast<uint32_t>(offset);
        const uint32_t slot = index % static_cast<uint32_t>(FDN_MATCH_SPOOL_CAPACITY);

        char key[16];
        snprintf(key, sizeof(key), "%s%u", SPOOL_SLOT_PREFIX, static_cast<unsigned>(slot));

        uint8_t buffer[MATCH_BINARY_SIZE] = {};
        if (storage->readBytes(FDN_MATCH_SPOOL_NAMESPACE, key, buffer, MATCH_BINARY_SIZE) !=
            MATCH_BINARY_SIZE) {
            return false;
        }
        out.deserialize(buffer);
        return true;
    }

    /** Handles an inbound MatchTransferPacket from a Connected PDN. */
    void onMatchCollectionPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
        if (!isConnectedPdnMac(fromMac)) {
            return;
        }
        if (!data || dataLen < sizeof(MatchTransferPacket)) {
            return;
        }
        if (data[0] != static_cast<uint8_t>(MatchCollectionCmd::RECORD)) {
            return;
        }

        MatchTransferPacket pkt{};
        memcpy(&pkt, data, sizeof(pkt));

        Match incoming{};
        incoming.deserialize(pkt.matchData);

        if (containsMatchUuid(incoming.getMatchUuid())) {
            sendAck(fromMac, pkt.matchSeq);
            return;
        }

        if (isFull()) {
            LOG_W("FDN_SPOOL", "Spool full (%u records); withholding ACK for seq %u",
                  static_cast<unsigned>(FDN_MATCH_SPOOL_CAPACITY),
                  static_cast<unsigned>(pkt.matchSeq));
            return;
        }

        if (!stageMatch(incoming)) {
            return;
        }

        sendAck(fromMac, pkt.matchSeq);
    }

    /** Uploads all currently staged matches as a protobuf batch via POST /quickdraw-matches. */
    bool uploadPendingBatch() {
        if (!wirelessManager || !storage || uploadInFlight) {
            return false;
        }
        const size_t batchCount = getStagedCount();
        if (batchCount == 0) {
            return false;
        }

        const uint32_t batchHead = getHead();
        uploadInFlight = true;
        lastRejections.clear();
        lastError.reset();

        MatchesApi::uploadQuickdrawMatches(
        wirelessManager, batchCount,
        [this, batchHead](size_t index, QuickdrawMatchRecord& record) -> bool {
            const uint32_t slot = (batchHead + static_cast<uint32_t>(index)) %
                                  static_cast<uint32_t>(FDN_MATCH_SPOOL_CAPACITY);
            char key[16];
            snprintf(key, sizeof(key), "%s%u", SPOOL_SLOT_PREFIX, static_cast<unsigned>(slot));
            uint8_t buffer[MATCH_BINARY_SIZE] = {};
            if (storage->readBytes(FDN_MATCH_SPOOL_NAMESPACE, key, buffer, MATCH_BINARY_SIZE) !=
                MATCH_BINARY_SIZE) {
                return false;
            }
            Match match{};
            match.deserialize(buffer);
            record = QuickdrawMatchCodec::fromMatch(match);
            return true;
        },
        [this](const QuickdrawMatchRejection& rejection) {
            lastRejections.push_back(rejection);
            char uuidText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
            rejection.matchId.format(uuidText);
            LOG_W("FDN_SPOOL", "Server rejected match[%u] %s (code=%d)",
                  static_cast<unsigned>(rejection.matchIndex), uuidText,
                  static_cast<int>(rejection.rawCode));
        },
        [this, batchHead, batchCount](const QuickdrawUploadResult& result) {
            const uint32_t newHead = batchHead + static_cast<uint32_t>(batchCount);
            storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY, newHead);
            lastUploadResult = result;
            completedUploadCount++;
            uploadInFlight = false;
        },
        [this](const WirelessErrorInfo& error) {
            lastError = error;
            uploadInFlight = false;
        });
        return true;
    }

    /** @return true while a batch upload HTTP request is in flight. */
    bool isUploadInFlight() const { return uploadInFlight; }
    /** @return Number of completed batch uploads. */
    size_t getCompletedUploadCount() const { return completedUploadCount; }
    /** @return Per-record rejections reported by the last batch upload. */
    const std::vector<QuickdrawMatchRejection>& getLastRejections() const { return lastRejections; }
    /** @return Summary result from the last completed batch upload. */
    const std::optional<QuickdrawUploadResult>& getLastUploadResult() const {
        return lastUploadResult;
    }
    /** @return Transport/HTTP error from the last failed batch upload. */
    const std::optional<WirelessErrorInfo>& getLastError() const { return lastError; }

private:
    bool isConnectedPdnMac(const uint8_t* mac) const {
        if (!rdc || !mac) return false;
        for (SerialIdentifier jack : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK,
                                      SerialIdentifier::INPUT_JACK_SECONDARY}) {
            if (rdc->getJackStatus(jack) == JackStatus::CONNECTED &&
                rdc->getPeerDeviceType(jack) == DeviceType::PDN) {
                const uint8_t* peer = rdc->getPeerMac(jack);
                if (peer && memcmp(peer, mac, 6) == 0) return true;
            }
        }
        return false;
    }

    bool containsMatchUuid(const UUIDBytes& uuid) const {
        if (uuid.isZero()) return false;
        const size_t count = getStagedCount();
        for (size_t i = 0; i < count; ++i) {
            Match staged{};
            if (readStagedMatch(i, staged) && staged.getMatchUuid() == uuid) {
                return true;
            }
        }
        return false;
    }

    bool stageMatch(const Match& match) {
        if (!storage || isFull()) return false;
        const uint32_t tail = getTail();
        const uint32_t slot = tail % static_cast<uint32_t>(FDN_MATCH_SPOOL_CAPACITY);

        char key[16];
        snprintf(key, sizeof(key), "%s%u", SPOOL_SLOT_PREFIX, static_cast<unsigned>(slot));

        uint8_t buffer[MATCH_BINARY_SIZE] = {};
        match.serialize(buffer);

        if (storage->writeBytes(FDN_MATCH_SPOOL_NAMESPACE, key, buffer, MATCH_BINARY_SIZE) !=
            MATCH_BINARY_SIZE) {
            return false;
        }
        if (storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_TAIL_KEY, tail + 1) !=
            sizeof(uint32_t)) {
            return false;
        }
        return true;
    }

    void sendAck(const uint8_t* toMac, uint32_t matchSeq) {
        if (!wirelessManager || !toMac) return;
        MatchTransferAckPacket ack{};
        ack.cmd = static_cast<uint8_t>(MatchCollectionCmd::ACK);
        ack.matchSeq = matchSeq;
        wirelessManager->sendEspNowData(toMac, PktType::kMatchCollection,
                                        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    }

    WirelessManager* wirelessManager = nullptr;
    StorageInterface* storage = nullptr;
    RemoteDeviceCoordinator* rdc = nullptr;
    bool uploadInFlight = false;
    size_t completedUploadCount = 0;
    std::vector<QuickdrawMatchRejection> lastRejections;
    std::optional<QuickdrawUploadResult> lastUploadResult;
    std::optional<WirelessErrorInfo> lastError;
};
