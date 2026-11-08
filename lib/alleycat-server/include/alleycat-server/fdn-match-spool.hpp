#pragma once

#include <cstdio>
#include <cstring>
#include <functional>
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

static_assert(FDN_MATCH_SPOOL_CAPACITY <= QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH,
              "one upload must be able to drain the whole spool");

/**
 * FDN persistent 64-slot match spool and batch uploader.
 *
 * Accepts MatchRecordPackets over PktType::kMatchCollection only from a PDN that is
 * Connected on an FDN jack, persists up to 64 records in NVS, acknowledges each
 * persisted record over PktType::kMatchCollectionAck (withholding the ack when full),
 * and uploads staged records through POST /quickdraw-matches when asked.
 */
class FdnMatchSpool {
public:
    /** Constructs the FDN match spool and registers the kMatchCollection handler. */
    FdnMatchSpool(WirelessManager* wirelessManager, StorageInterface* storage,
                  RemoteDeviceCoordinator* rdc)
        : wirelessManager(wirelessManager)
        , storage(storage)
        , rdc(rdc) {
        if (this->wirelessManager) {
            this->wirelessManager->setEspNowPacketHandler(
            PktType::kMatchCollection,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                static_cast<FdnMatchSpool*>(ctx)->onMatchRecord(fromMac, data, dataLen);
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

    /** @return Number of staged matches awaiting upload (0..64). */
    size_t getStagedCount() const { return getTail() - getHead(); }

    /** Reads the staged match at @p offset from the oldest staged record. */
    bool readStagedMatch(size_t offset, Match& out) const {
        if (offset >= getStagedCount()) return false;
        return readSlot(getHead() + static_cast<uint32_t>(offset), out);
    }

    /** Handles an inbound MatchRecordPacket from a Connected PDN. */
    void onMatchRecord(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
        if (!storage || !rdc || !rdc->isConnectedPeer(DeviceType::PDN, fromMac)) {
            return;
        }
        if (!data || dataLen != sizeof(MatchRecordPacket)) {
            LOG_W("FDN_SPOOL", "Dropping match record of %zu bytes", dataLen);
            return;
        }

        MatchRecordPacket pkt{};
        memcpy(&pkt, data, sizeof(pkt));

        Match incoming{};
        incoming.deserialize(pkt.matchData);

        // A lost ack makes the PDN resend a record that is already staged.
        if (!containsMatchUuid(incoming.getMatchUuid())) {
            if (getStagedCount() >= FDN_MATCH_SPOOL_CAPACITY) {
                LOG_W("FDN_SPOOL", "Spool full; withholding ack for seq %u",
                      static_cast<unsigned>(pkt.matchSeq));
                return;
            }
            if (!stageMatch(pkt.matchData)) {
                return;
            }
        }
        sendAck(fromMac, pkt.matchSeq);
    }

    /**
     * Uploads the staged matches as one POST /quickdraw-matches batch. The server
     * keeps the first report of a match ID, so a batch is retired on any decodable
     * response, rejections included, and kept for retry on any error, an
     * undecodable response included.
     *
     * @return false when nothing was queued; @p onComplete then never fires.
     */
    bool uploadPendingBatch(const std::function<void(bool uploaded)>& onComplete) {
        if (!wirelessManager || !storage || uploadInFlight) {
            return false;
        }
        dropUnreadableHead();
        // Stop short of an unreadable record so it fails no batch; once the records
        // ahead of it are retired it is the head, and dropUnreadableHead discards it.
        size_t batchCount = 0;
        Match probe{};
        while (batchCount < getStagedCount() && readStagedMatch(batchCount, probe)) {
            ++batchCount;
        }
        if (batchCount == 0) {
            return false;
        }

        const uint32_t batchHead = getHead();
        uploadInFlight = true;

        MatchesApi::uploadQuickdrawMatches(
        wirelessManager, batchCount,
        [this, batchHead](size_t index, QuickdrawMatchRecord& record) -> bool {
            Match match{};
            if (!readSlot(batchHead + static_cast<uint32_t>(index), match)) {
                return false;
            }
            record = QuickdrawMatchCodec::fromMatch(match);
            return true;
        },
        [](const QuickdrawMatchRejection& rejection) {
            char uuidText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
            rejection.matchId.format(uuidText);
            LOG_W("FDN_SPOOL", "Server rejected match[%u] %s (code=%d)",
                  static_cast<unsigned>(rejection.matchIndex), uuidText,
                  static_cast<int>(rejection.rawCode));
        },
        [this, batchHead, batchCount, onComplete](const QuickdrawUploadResult&) {
            storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY,
                               batchHead + static_cast<uint32_t>(batchCount));
            uploadInFlight = false;
            if (onComplete) onComplete(true);
        },
        [this, onComplete](const WirelessErrorInfo& error) {
            LOG_W("FDN_SPOOL", "Match upload failed: %s", error.message.c_str());
            uploadInFlight = false;
            if (onComplete) onComplete(false);
        });
        return true;
    }

private:
    uint32_t getTail() const {
        return storage ? storage->readUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_TAIL_KEY, 0) : 0;
    }

    // Clamped so a torn head/tail pair from NVS corruption reads as at most a full spool.
    uint32_t getHead() const {
        if (!storage) return 0;
        const uint32_t tail = getTail();
        const uint32_t head = storage->readUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY, 0);
        if (head > tail) return tail;
        if (tail - head > FDN_MATCH_SPOOL_CAPACITY) {
            return tail - static_cast<uint32_t>(FDN_MATCH_SPOOL_CAPACITY);
        }
        return head;
    }

    static void slotKey(uint32_t index, char (&key)[16]) {
        snprintf(key, sizeof(key), "%s%u", SPOOL_SLOT_PREFIX,
                 static_cast<unsigned>(index % FDN_MATCH_SPOOL_CAPACITY));
    }

    bool readSlot(uint32_t index, Match& out) const {
        char key[16];
        slotKey(index, key);
        uint8_t buffer[MATCH_BINARY_SIZE] = {};
        if (storage->readBytes(FDN_MATCH_SPOOL_NAMESPACE, key, buffer, MATCH_BINARY_SIZE) !=
            MATCH_BINARY_SIZE) {
            return false;
        }
        out.deserialize(buffer);
        return true;
    }

    // An unreadable record would fail every batch it heads, so it is dropped instead.
    void dropUnreadableHead() {
        Match probe{};
        while (getStagedCount() > 0 && !readSlot(getHead(), probe)) {
            LOG_E("FDN_SPOOL", "Dropping unreadable spool slot %u",
                  static_cast<unsigned>(getHead() % FDN_MATCH_SPOOL_CAPACITY));
            storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY, getHead() + 1);
        }
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

    bool stageMatch(const uint8_t (&record)[MATCH_BINARY_SIZE]) {
        const uint32_t tail = getTail();
        char key[16];
        slotKey(tail, key);
        if (storage->writeBytes(FDN_MATCH_SPOOL_NAMESPACE, key, record, MATCH_BINARY_SIZE) !=
            MATCH_BINARY_SIZE) {
            return false;
        }
        return storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_TAIL_KEY, tail + 1) ==
               sizeof(uint32_t);
    }

    void sendAck(const uint8_t* toMac, uint32_t matchSeq) {
        MatchRecordAckPacket ack{};
        ack.matchSeq = matchSeq;
        wirelessManager->sendEspNowData(toMac, PktType::kMatchCollectionAck,
                                        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    }

    WirelessManager* wirelessManager = nullptr;
    StorageInterface* storage = nullptr;
    RemoteDeviceCoordinator* rdc = nullptr;
    bool uploadInFlight = false;
};
