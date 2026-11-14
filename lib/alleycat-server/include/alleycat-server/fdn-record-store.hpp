#pragma once

#include <cstdio>
#include <cstring>
#include <functional>
#include "alleycat-server/matches-api.hpp"
#include "alleycat-server/quickdraw-match-codec.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/wireless-manager.hpp"
#include "game/match.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/reliable-channel.hpp"

inline constexpr const char FDN_RECORD_STORE_NAMESPACE[] = "fdn_records";
inline constexpr size_t FDN_CRASH_RECORD_CAPACITY = 32;
inline constexpr size_t FDN_MATCH_RECORD_CAPACITY = 64;

static_assert(FDN_MATCH_RECORD_CAPACITY <= QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH,
              "one upload must be able to drain every stored match record");

// One record as the FDN's storage holds it: what the PDN's frame carried, plus
// who sent it. The struct is the stored layout.
template <class Content> struct FdnStoredEntry {
    uint8_t senderMac[6];
    uint8_t kind;
    uint32_t recordNumber;
    Content content;
} __attribute__((packed));
static_assert(sizeof(FdnStoredEntry<CrashRecordContent>) == 53, "stored crash entry size");
static_assert(sizeof(FdnStoredEntry<MatchRecordContent>) == 139, "stored match entry size");

// One record kind's intake and its ring of slots in storage, owned by
// FdnRecordStore. head and tail count records ever retired and ever stored; a
// record's slot is its count modulo CAPACITY. Storage is written only here, so
// both are kept in memory. storageRefused is the store's one latch, shared by
// its rings because they share the partition whose refusal sets it.
template <class Traits, size_t CAPACITY> class FdnRecordRing {
public:
    using Frame = typename Traits::Frame;
    using Entry = FdnStoredEntry<typename Traits::Content>;

    /**
     * Loads the ring from storage and claims the kind's record and
     * acknowledgement types. Every storage key of the ring starts with @p keyPrefix.
     */
    FdnRecordRing(WirelessManager* wirelessManager, StorageInterface* storage, bool& storageRefused,
                  const char* keyPrefix)
        : storage(storage)
        , storageRefused(storageRefused)
        , keyPrefix(keyPrefix)
        , frameChannel(wirelessManager, Traits::FRAME_PACKET_TYPE)
        , ackChannel(wirelessManager, Traits::ACK_PACKET_TYPE) {
        loadFromStorage();
        frameChannel.onReceive(
        [this](const uint8_t* fromMac, const Frame& frame) { onRecordFrame(fromMac, frame); });
    }

    /** @return Number of records held. */
    uint32_t getStoredCount() const { return tail - head; }

    /** @return The count of the oldest held record. */
    uint32_t getHead() const { return head; }

    /**
     * Reads the entry counted @p index. An entry of the wrong length or kind is
     * unreadable, the same as a missing one.
     */
    bool readEntry(uint32_t index, Entry& entry) const {
        char key[16];
        slotKey(index, key);
        return storage->readBytes(FDN_RECORD_STORE_NAMESPACE, key,
                                  reinterpret_cast<uint8_t*>(&entry),
                                  sizeof(entry)) == sizeof(entry) &&
               entry.kind == static_cast<uint8_t>(Traits::KIND);
    }

    /**
     * @return true when the entry is held afterwards, which is when it may be
     * acknowledged: newly stored, or byte-identical to one already held. An
     * entry that shares a held one's sender and number but differs in bytes
     * or, for a match record, tag is stored separately: taking it for a
     * duplicate would let a junk frame stand in for the real one.
     * @p recordCheck receives the check over the entry's content.
     */
    bool storeRecord(const Entry& entry, uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
        computeRecordCheck(entry.content, recordCheck);
        const uint32_t recordNumber = entry.recordNumber;
        // Before the capacity check: a PDN that lost the acknowledgement resends
        // a record that is already held, full or not.
        if (holdsEntry(entry, recordCheck)) {
            return true;
        }
        // A refused blob write still costs the flash a page erase, and a PDN
        // offers its record again on every beacon and every FDN connection, so
        // storage is not asked again until a slot is freed.
        if (storageRefused) {
            LOG_W("FDN_RECORDS",
                  "Storage refused a record earlier; record %u of kind %u is not stored",
                  static_cast<unsigned>(recordNumber), static_cast<unsigned>(Traits::KIND));
            return false;
        }
        if (tail - head >= CAPACITY) {
            LOG_W("FDN_RECORDS", "No room for kind %u; record %u is not acknowledged",
                  static_cast<unsigned>(Traits::KIND), static_cast<unsigned>(recordNumber));
            return false;
        }

        char key[16];
        slotKey(tail, key);
        if (storage->writeBytes(FDN_RECORD_STORE_NAMESPACE, key,
                                reinterpret_cast<const uint8_t*>(&entry),
                                sizeof(entry)) != sizeof(entry)) {
            LOG_E("FDN_RECORDS", "Storing record %u of kind %u failed",
                  static_cast<unsigned>(recordNumber), static_cast<unsigned>(Traits::KIND));
            storageRefused = true;
            return false;
        }
        char tailKey[16];
        counterKey("tail", tailKey);
        if (storage->writeUInt(FDN_RECORD_STORE_NAMESPACE, tailKey, tail + 1) != sizeof(uint32_t)) {
            LOG_E("FDN_RECORDS", "Committing record %u of kind %u failed",
                  static_cast<unsigned>(recordNumber), static_cast<unsigned>(Traits::KIND));
            // Outside [head, tail) nothing would ever retire this blob.
            storage->remove(FDN_RECORD_STORE_NAMESPACE, key);
            storageRefused = true;
            return false;
        }
        HeldRecord& held = heldRecords[tail % CAPACITY];
        memcpy(held.senderMac, entry.senderMac, sizeof(held.senderMac));
        held.recordNumber = recordNumber;
        memcpy(held.recordCheck, recordCheck, RECORD_CHECK_SIZE);
        ++tail;
        return true;
    }

    /**
     * Erases @p count slots from @p from and moves the head past them, which
     * frees room at once. A head write that storage refuses is made again by
     * the next call; until then, or after a restart, the stored head rests on
     * erased slots, which read as unreadable and are dropped. Erasing first is
     * what makes that safe: the other order could leave blobs nothing points at.
     */
    void retire(uint32_t from, uint32_t count) {
        for (uint32_t index = from; index != from + count; ++index) {
            // A retired record's blob still takes storage entries until its key is
            // erased, and a full match ring of them is most of the partition.
            char key[16];
            slotKey(index, key);
            storage->remove(FDN_RECORD_STORE_NAMESPACE, key);
            heldRecords[index % CAPACITY] = HeldRecord{};
        }
        storageRefused = false;
        head = from + count;
        char headKey[16];
        counterKey("head", headKey);
        if (storage->writeUInt(FDN_RECORD_STORE_NAMESPACE, headKey, head) != sizeof(uint32_t)) {
            LOG_E("FDN_RECORDS", "Storing the head of kind %u failed",
                  static_cast<unsigned>(Traits::KIND));
        }
    }

private:
    // Spares each frame a storage read per held record; it only narrows the search.
    struct HeldRecord {
        uint8_t senderMac[6];
        uint32_t recordNumber;
        uint8_t recordCheck[RECORD_CHECK_SIZE];
    };

    void slotKey(uint32_t index, char (&key)[16]) const {
        snprintf(key, sizeof(key), "%s%u", keyPrefix, static_cast<unsigned>(index % CAPACITY));
    }

    // The key of the ring's head or tail count: the prefix, then @p name.
    void counterKey(const char* name, char (&key)[16]) const {
        snprintf(key, sizeof(key), "%s%s", keyPrefix, name);
    }

    void loadFromStorage() {
        char tailKey[16];
        counterKey("tail", tailKey);
        char headKey[16];
        counterKey("head", headKey);
        tail = storage->readUInt(FDN_RECORD_STORE_NAMESPACE, tailKey, 0);
        head = storage->readUInt(FDN_RECORD_STORE_NAMESPACE, headKey, 0);
        // Clamped so a torn head/tail pair from storage corruption reads as at most a full ring.
        if (head > tail) head = tail;
        if (tail - head > CAPACITY) head = tail - static_cast<uint32_t>(CAPACITY);

        Entry entry{};
        for (uint32_t index = head; index != tail; ++index) {
            if (!readEntry(index, entry)) continue;
            HeldRecord& held = heldRecords[index % CAPACITY];
            memcpy(held.senderMac, entry.senderMac, sizeof(held.senderMac));
            held.recordNumber = entry.recordNumber;
            computeRecordCheck(entry.content, held.recordCheck);
        }
    }

    bool holdsEntry(const Entry& entry, const uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) const {
        const uint32_t recordNumber = entry.recordNumber;
        for (uint32_t index = head; index != tail; ++index) {
            const HeldRecord& held = heldRecords[index % CAPACITY];
            if (held.recordNumber != recordNumber ||
                memcmp(held.senderMac, entry.senderMac, sizeof(held.senderMac)) != 0 ||
                memcmp(held.recordCheck, recordCheck, RECORD_CHECK_SIZE) != 0) {
                continue;
            }
            // Two frames can be built to share a check, so the stored bytes decide.
            Entry stored{};
            if (readEntry(index, stored) && memcmp(&stored, &entry, sizeof(entry)) == 0) {
                return true;
            }
        }
        return false;
    }

    // A frame whose kind is not this ring's is dropped: the channel fixed its
    // length, so its bytes are not a record of the kind it claims.
    void onRecordFrame(const uint8_t* fromMac, const Frame& frame) {
        if (frame.kind != static_cast<uint8_t>(Traits::KIND)) {
            LOG_W("FDN_RECORDS", "Dropping a record frame of kind %u on the channel for kind %u",
                  static_cast<unsigned>(frame.kind), static_cast<unsigned>(Traits::KIND));
            return;
        }

        Entry entry{};
        memcpy(entry.senderMac, fromMac, sizeof(entry.senderMac));
        entry.kind = frame.kind;
        entry.recordNumber = frame.recordNumber;
        entry.content = frame.content;

        uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
        if (!storeRecord(entry, recordCheck)) {
            return;
        }

        RecordAckPacket ack{};
        ack.kind = frame.kind;
        ack.recordNumber = frame.recordNumber;
        memcpy(ack.recordCheck, recordCheck, RECORD_CHECK_SIZE);
        ackChannel.sendReliable(fromMac, ack);
    }

    StorageInterface* storage = nullptr;
    bool& storageRefused;
    const char* keyPrefix = nullptr;
    ReliableChannel<Frame> frameChannel;
    ReliableChannel<RecordAckPacket> ackChannel;
    uint32_t head = 0;
    uint32_t tail = 0;
    HeldRecord heldRecords[CAPACITY] = {};
};

/**
 * The FDN's non-volatile store of crash records and match records relayed by
 * PDNs. It takes a record frame from any PDN, acknowledges it only once the
 * record is stored, and uploads the match records as one batch. Each record kind
 * has its own bound, but both share one storage partition. The FDN holds no
 * record keys: a match record's tag is stored as received and never checked.
 */
class FdnRecordStore {
public:
    /** Loads what the storage holds and claims the record and acknowledgement packet types. */
    FdnRecordStore(WirelessManager* wirelessManager, StorageInterface* storage)
        : wirelessManager(wirelessManager)
        , crashRing(wirelessManager, storage, storageRefused, "c_")
        , matchRing(wirelessManager, storage, storageRefused, "m_") {}

    /** @return Number of records of @p kind held. */
    size_t getStoredCount(RecordKind kind) const {
        return kind == RecordKind::CRASH ? crashRing.getStoredCount() : matchRing.getStoredCount();
    }

    /**
     * Uploads the stored match records as one POST /quickdraw-matches batch. Any
     * decodable response retires the batch, so its records stop counting as held
     * at once: its matches are stored, already stored, or rejected for good. Any
     * error, an undecodable response included, keeps the batch; the server never
     * overwrites a stored match, so the retry is safe.
     *
     * @param onComplete Told whether the batch was uploaded and retired.
     * @return false when no upload was attempted because no readable match record
     * is held; @p onComplete then never fires. Otherwise it may fire before this returns.
     */
    bool uploadPendingBatch(const std::function<void(bool uploaded)>& onComplete) {
        dropUnreadableMatchHead();
        // Stop short of an unreadable record so it fails no batch; once the records
        // ahead of it are retired it is the head, and dropUnreadableMatchHead discards it.
        const uint32_t batchHead = matchRing.getHead();
        const uint32_t storedCount = matchRing.getStoredCount();
        uint32_t batchCount = 0;
        Match probe{};
        while (batchCount < storedCount && readMatch(batchHead + batchCount, probe)) {
            ++batchCount;
        }
        if (batchCount == 0) {
            return false;
        }

        MatchesApi::uploadQuickdrawMatches(
        wirelessManager, batchCount,
        [this, batchHead](size_t index, QuickdrawMatchRecord& record) -> bool {
            Match match{};
            if (!readMatch(batchHead + static_cast<uint32_t>(index), match)) {
                return false;
            }
            record = QuickdrawMatchCodec::fromMatch(match);
            return true;
        },
        [](const QuickdrawMatchRejection& rejection) {
            char uuidText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
            rejection.matchId.format(uuidText);
            LOG_W("FDN_RECORDS", "Server rejected match[%u] %s (code=%d)",
                  static_cast<unsigned>(rejection.matchIndex), uuidText,
                  static_cast<int>(rejection.rawCode));
        },
        [this, batchHead, batchCount, onComplete](const QuickdrawUploadResult&) {
            matchRing.retire(batchHead, batchCount);
            if (onComplete) onComplete(true);
        },
        [onComplete](const WirelessErrorInfo& error) {
            LOG_W("FDN_RECORDS", "Match upload failed: %s", error.message.c_str());
            if (onComplete) onComplete(false);
        });
        return true;
    }

private:
    using CrashRing = FdnRecordRing<CrashRecordTraits, FDN_CRASH_RECORD_CAPACITY>;
    using MatchRing = FdnRecordRing<MatchRecordTraits, FDN_MATCH_RECORD_CAPACITY>;

    bool readMatch(uint32_t index, Match& out) const {
        MatchRing::Entry entry{};
        if (!matchRing.readEntry(index, entry)) {
            return false;
        }
        out.deserialize(entry.content.recordBytes);
        return true;
    }

    // An unreadable record would fail every batch it heads, so the unreadable
    // run at the head is dropped instead, in one retirement.
    void dropUnreadableMatchHead() {
        Match probe{};
        uint32_t unreadableCount = 0;
        while (unreadableCount < matchRing.getStoredCount() &&
               !readMatch(matchRing.getHead() + unreadableCount, probe)) {
            ++unreadableCount;
        }
        if (unreadableCount == 0) {
            return;
        }
        LOG_E("FDN_RECORDS", "Dropping %u unreadable match record(s) from %u",
              static_cast<unsigned>(unreadableCount), static_cast<unsigned>(matchRing.getHead()));
        matchRing.retire(matchRing.getHead(), unreadableCount);
    }

    WirelessManager* wirelessManager = nullptr;
    // Set by a failed write in either ring and cleared when either frees a slot:
    // the rings share one partition, so room freed by one is room for the other.
    // Declared before the rings, which hold a reference to it.
    bool storageRefused = false;
    CrashRing crashRing;
    MatchRing matchRing;
};
