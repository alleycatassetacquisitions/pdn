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
 * FDN's NVS-backed spool of collected matches: acks a cabled PDN's record only once
 * it is persisted, and uploads the spool as one batch.
 */
class FdnMatchSpool {
public:
    /** Constructs the FDN match spool and registers the kMatchCollection handler. */
    FdnMatchSpool(WirelessManager* wirelessManager, StorageInterface* storage,
                  RemoteDeviceCoordinator* rdc)
        : wirelessManager(wirelessManager)
        , storage(storage)
        , rdc(rdc) {
        loadStagedUuids();
        wirelessManager->setEspNowPacketHandler(
        PktType::kMatchCollection,
        [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<FdnMatchSpool*>(ctx)->onMatchRecord(fromMac, data, dataLen);
        },
        this);
    }

    /** Clears the kMatchCollection packet handler on destruction. */
    ~FdnMatchSpool() { wirelessManager->clearEspNowPacketHandler(PktType::kMatchCollection); }

    /** @return Number of staged matches awaiting upload. */
    size_t getStagedCount() const { return getTail() - getHead(); }

    /** Handles an inbound MatchRecordPacket from a Connected PDN. */
    void onMatchRecord(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
        if (!rdc->isConnectedPeer(DeviceType::PDN, fromMac)) {
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

        // A lost ack makes the PDN resend a record that is already staged, full spool or not.
        if (!containsMatchUuid(incoming.getMatchUuid())) {
            if (getStagedCount() >= FDN_MATCH_SPOOL_CAPACITY) {
                LOG_W("FDN_SPOOL", "Spool full; withholding ack for seq %u",
                      static_cast<unsigned>(pkt.matchSeq));
                return;
            }
            if (!stageMatch(pkt.matchData, incoming.getMatchUuid())) {
                return;
            }
        }
        sendAck(fromMac, pkt.matchSeq);
    }

    /**
     * Uploads the staged matches as one POST /quickdraw-matches batch. Any decodable
     * response retires the batch: its matches are stored, already stored, or
     * rejected for good. Any error, an undecodable response included, keeps the
     * batch; the server never overwrites a stored match, so the retry is safe.
     *
     * @return false when no upload was attempted because nothing readable is staged;
     * @p onComplete then never fires. Otherwise it may fire before this returns.
     */
    bool uploadPendingBatch(const std::function<void(bool uploaded)>& onComplete) {
        dropUnreadableHead();
        // Stop short of an unreadable record so it fails no batch; once the records
        // ahead of it are retired it is the head, and dropUnreadableHead discards it.
        const uint32_t batchHead = getHead();
        uint32_t batchCount = 0;
        Match probe{};
        while (batchCount < getStagedCount() && readSlot(batchHead + batchCount, probe)) {
            ++batchCount;
        }
        if (batchCount == 0) {
            return false;
        }

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
            if (storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY,
                                   batchHead + batchCount) == sizeof(uint32_t)) {
                for (uint32_t index = batchHead; index < batchHead + batchCount; ++index) {
                    eraseSlot(index);
                }
            }
            if (onComplete) onComplete(true);
        },
        [onComplete](const WirelessErrorInfo& error) {
            LOG_W("FDN_SPOOL", "Match upload failed: %s", error.message.c_str());
            if (onComplete) onComplete(false);
        });
        return true;
    }

private:
    uint32_t getTail() const {
        return storage->readUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_TAIL_KEY, 0);
    }

    // Clamped so a torn head/tail pair from NVS corruption reads as at most a full spool.
    uint32_t getHead() const {
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

    // A retired record's blob still takes NVS entries until its key is erased, and
    // a full spool of them is most of the partition.
    void eraseSlot(uint32_t index) {
        char key[16];
        slotKey(index, key);
        storage->remove(FDN_MATCH_SPOOL_NAMESPACE, key);
    }

    // An unreadable record would fail every batch it heads, so it is dropped instead.
    void dropUnreadableHead() {
        Match probe{};
        while (getStagedCount() > 0 && !readSlot(getHead(), probe)) {
            const uint32_t head = getHead();
            LOG_E("FDN_SPOOL", "Dropping unreadable spool slot %u",
                  static_cast<unsigned>(head % FDN_MATCH_SPOOL_CAPACITY));
            if (storage->writeUInt(FDN_MATCH_SPOOL_NAMESPACE, SPOOL_HEAD_KEY, head + 1) !=
                sizeof(uint32_t)) {
                return;
            }
            eraseSlot(head);
        }
    }

    // NVS stays the record of truth; this copy only spares the per-resend dedup a
    // slot-by-slot NVS scan. Retired slots fall outside [head, tail) and need no upkeep.
    void loadStagedUuids() {
        const uint32_t tail = getTail();
        for (uint32_t index = getHead(); index < tail; ++index) {
            Match staged{};
            stagedUuids[index % FDN_MATCH_SPOOL_CAPACITY] =
            readSlot(index, staged) ? staged.getMatchUuid() : UUIDBytes{};
        }
    }

    bool containsMatchUuid(const UUIDBytes& uuid) const {
        if (uuid.isZero()) return false;
        const uint32_t tail = getTail();
        for (uint32_t index = getHead(); index < tail; ++index) {
            if (stagedUuids[index % FDN_MATCH_SPOOL_CAPACITY] == uuid) return true;
        }
        return false;
    }

    bool stageMatch(const uint8_t (&record)[MATCH_BINARY_SIZE], const UUIDBytes& uuid) {
        const uint32_t tail = getTail();
        char key[16];
        slotKey(tail, key);
        if (storage->writeBytes(FDN_MATCH_SPOOL_NAMESPACE, key, record, MATCH_BINARY_SIZE) !=
            MATCH_BINARY_SIZE) {
            return false;
        }
        stagedUuids[tail % FDN_MATCH_SPOOL_CAPACITY] = uuid;
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
    UUIDBytes stagedUuids[FDN_MATCH_SPOOL_CAPACITY] = {};
};
