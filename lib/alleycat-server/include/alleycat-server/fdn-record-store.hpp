#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include "alleycat-server/fdn-stored-entry.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/reliable-channel.hpp"

inline constexpr const char FDN_RECORD_STORE_NAMESPACE[] = "fdn_records";
inline constexpr size_t FDN_CRASH_RECORD_CAPACITY = 32;
inline constexpr size_t FDN_MATCH_RECORD_CAPACITY = 64;
// Legacy matches take at most this many of the FDN_MATCH_RECORD_CAPACITY match
// slots, so a PDN's backlog from older firmware never crowds current matches out.
inline constexpr size_t FDN_LEGACY_MATCH_RECORD_CAPACITY = 32;

// One record kind's ring of slots in storage, owned by FdnRecordStore. head and
// tail count records ever retired and ever stored; a record's slot is its count
// modulo CAPACITY. Storage is written only here, so both are kept in memory.
// storageRefused is the latch the ring sets when storage refuses a write and
// clears when it frees a slot; the store decides which rings share one.
template <class Content, size_t CAPACITY> class FdnRecordRing {
public:
    using Entry = FdnStoredEntry<Content>;

    /**
     * Loads the ring of @p kind from storage. Every storage key of the ring
     * starts with @p keyPrefix.
     */
    FdnRecordRing(StorageInterface* storage, bool& storageRefused, const char* keyPrefix,
                  RecordKind kind)
        : storage(storage)
        , storageRefused(storageRefused)
        , keyPrefix(keyPrefix)
        , kind(kind) {
        loadFromStorage();
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
               entry.kind == static_cast<uint8_t>(kind);
    }

    /** @return true when @p index counts a record that is held. */
    bool holds(uint32_t index) const { return index - head < tail - head; }

    /**
     * Retires the unreadable run at the head, in one retirement. An unreadable
     * entry there could never be uploaded and would block every entry behind it.
     */
    void dropUnreadableHead() {
        Entry entry{};
        const uint32_t storedCount = tail - head;
        uint32_t unreadableCount = 0;
        while (unreadableCount < storedCount && !readEntry(head + unreadableCount, entry)) {
            ++unreadableCount;
        }
        if (unreadableCount > 0) {
            LOG_E("FDN_RECORDS", "Dropping %u unreadable record(s) of kind %u from %u",
                  static_cast<unsigned>(unreadableCount), static_cast<unsigned>(kind),
                  static_cast<unsigned>(head));
            retire(head, unreadableCount);
        }
    }

    /**
     * @return true when the entry is held afterwards, which is when it may be
     * acknowledged: newly stored, or byte-identical to one already held. An
     * entry that shares a held one's sender and number but differs in bytes
     * or, for a match record, tag is stored separately: taking it for a
     * duplicate would let a junk frame stand in for the real one. A new entry
     * is refused while the ring holds @p roomLimit or more, at most CAPACITY.
     * @p recordCheck receives the check over the entry's content.
     */
    bool storeRecord(const Entry& entry, uint8_t (&recordCheck)[RECORD_CHECK_SIZE],
                     size_t roomLimit) {
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
                  static_cast<unsigned>(recordNumber), static_cast<unsigned>(kind));
            return false;
        }
        if (tail - head >= std::min(roomLimit, CAPACITY)) {
            LOG_W("FDN_RECORDS", "No room for kind %u; record %u is not acknowledged",
                  static_cast<unsigned>(kind), static_cast<unsigned>(recordNumber));
            return false;
        }

        char key[16];
        slotKey(tail, key);
        if (storage->writeBytes(FDN_RECORD_STORE_NAMESPACE, key,
                                reinterpret_cast<const uint8_t*>(&entry),
                                sizeof(entry)) != sizeof(entry)) {
            LOG_E("FDN_RECORDS", "Storing record %u of kind %u failed",
                  static_cast<unsigned>(recordNumber), static_cast<unsigned>(kind));
            storageRefused = true;
            return false;
        }
        char tailKey[16];
        counterKey("tail", tailKey);
        if (storage->writeUInt(FDN_RECORD_STORE_NAMESPACE, tailKey, tail + 1) != sizeof(uint32_t)) {
            LOG_E("FDN_RECORDS", "Committing record %u of kind %u failed",
                  static_cast<unsigned>(recordNumber), static_cast<unsigned>(kind));
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
            LOG_E("FDN_RECORDS", "Storing the head of kind %u failed", static_cast<unsigned>(kind));
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

    StorageInterface* storage = nullptr;
    bool& storageRefused;
    const char* keyPrefix = nullptr;
    RecordKind kind;
    uint32_t head = 0;
    uint32_t tail = 0;
    HeldRecord heldRecords[CAPACITY] = {};
};

// One record channel's intake: it hands each frame to @p storeEntry, which
// files it by kind, and acknowledges only what that reports held.
template <class Traits> class FdnRecordIntake {
public:
    using Frame = typename Traits::Frame;
    using Entry = FdnStoredEntry<typename Traits::Content>;
    using StoreEntry = std::function<bool(const Entry&, uint8_t (&)[RECORD_CHECK_SIZE])>;

    /** Claims the channel's record and acknowledgement packet types. */
    FdnRecordIntake(WirelessManager* wirelessManager, StoreEntry storeEntry)
        : storeEntry(std::move(storeEntry))
        , frameChannel(wirelessManager, Traits::FRAME_PACKET_TYPE)
        , ackChannel(wirelessManager, Traits::ACK_PACKET_TYPE) {
        frameChannel.onReceive(
        [this](const uint8_t* fromMac, const Frame& frame) { onRecordFrame(fromMac, frame); });
    }

private:
    void onRecordFrame(const uint8_t* fromMac, const Frame& frame) {
        Entry entry{};
        memcpy(entry.senderMac, fromMac, sizeof(entry.senderMac));
        entry.kind = frame.kind;
        entry.recordNumber = frame.recordNumber;
        entry.content = frame.content;

        uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
        if (!storeEntry(entry, recordCheck)) {
            return;
        }

        RecordAckPacket ack{};
        ack.kind = frame.kind;
        ack.recordNumber = frame.recordNumber;
        memcpy(ack.recordCheck, recordCheck, RECORD_CHECK_SIZE);
        ackChannel.sendReliable(fromMac, ack);
    }

    StoreEntry storeEntry;
    ReliableChannel<Frame> frameChannel;
    ReliableChannel<RecordAckPacket> ackChannel;
};

/**
 * The FDN's non-volatile store of crash records and match records relayed by
 * PDNs. It takes a record frame from any PDN, acknowledges it only once the
 * record is stored, and hands the records out to be uploaded: the match
 * records as one batch, the crash records oldest first, one at a time. Crash
 * records have their own bound; current and legacy matches share
 * FDN_MATCH_RECORD_CAPACITY; all share one storage partition. The FDN holds no
 * record keys: a match record's tag is stored as received and never checked.
 * Legacy matches arrive on the match channel and are held in a ring of their
 * own, which the uploader leaves stored until the server takes them.
 */
class FdnRecordStore {
public:
    /** Loads what the storage holds and claims the record and acknowledgement packet types. */
    FdnRecordStore(WirelessManager* wirelessManager, StorageInterface* storage)
        : crashRing(storage, storageRefused, "c_", RecordKind::CRASH)
        , matchRing(storage, storageRefused, "m_", RecordKind::MATCH)
        , legacyMatchRing(storage, legacyStorageRefused, "l_", RecordKind::LEGACY_MATCH)
        , crashIntake(
          wirelessManager,
          [this](const FdnStoredCrashEntry& entry, uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
              return storeCrashEntry(entry, recordCheck);
          })
        , matchIntake(wirelessManager, [this](const FdnStoredMatchEntry& entry,
                                              uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
            return storeMatchEntry(entry, recordCheck);
        }) {}

    /** @return Number of records of @p kind held. */
    size_t getStoredCount(RecordKind kind) const {
        switch (kind) {
            case RecordKind::CRASH: return crashRing.getStoredCount();
            case RecordKind::MATCH: return matchRing.getStoredCount();
            case RecordKind::LEGACY_MATCH: return legacyMatchRing.getStoredCount();
        }
        return 0;
    }

    /**
     * Drops the unreadable records at the head of @p kind, which could never
     * be uploaded and would block every record behind them. The reads below
     * change nothing, so whoever drains the store asks for this first.
     */
    void dropUnreadableOldest(RecordKind kind) {
        switch (kind) {
            case RecordKind::CRASH: crashRing.dropUnreadableHead(); return;
            case RecordKind::MATCH: matchRing.dropUnreadableHead(); return;
            case RecordKind::LEGACY_MATCH: legacyMatchRing.dropUnreadableHead(); return;
        }
    }

    /**
     * Reads the oldest relayed crash record held, as it was stored, and writes
     * to @p position the place retireCrash() takes back.
     * @return false when no crash record is held or the oldest is unreadable.
     */
    bool readOldestCrash(FdnStoredCrashEntry& entry, uint32_t& position) const {
        position = crashRing.getHead();
        return crashRing.holds(position) && crashRing.readEntry(position, entry);
    }

    /**
     * Retires the relayed crash record read at @p position; its slot is free
     * for a new record at once.
     * @return false, retiring nothing, when it is no longer the oldest held.
     */
    bool retireCrash(uint32_t position) {
        if (!crashRing.holds(position) || position != crashRing.getHead()) {
            LOG_E("FDN_RECORDS", "Crash record at %u is not the oldest held; not retired",
                  static_cast<unsigned>(position));
            return false;
        }
        crashRing.retire(position, 1);
        legacyStorageRefused = false;
        return true;
    }

    /**
     * Counts the readable match records from the oldest on, the first of them
     * at @p position; it stops short of an unreadable record, which
     * dropUnreadableOldest() drops once it is the oldest.
     */
    size_t countReadableMatchBatch(uint32_t& position) const {
        FdnStoredMatchEntry entry{};
        position = matchRing.getHead();
        const uint32_t storedCount = matchRing.getStoredCount();
        uint32_t batchCount = 0;
        while (batchCount < storedCount && matchRing.readEntry(position + batchCount, entry)) {
            ++batchCount;
        }
        return batchCount;
    }

    /**
     * Reads the relayed match record at @p position, as it was stored.
     * @return false when no readable match record is held there.
     */
    bool readMatch(uint32_t position, FdnStoredMatchEntry& entry) const {
        return matchRing.holds(position) && matchRing.readEntry(position, entry);
    }

    /**
     * Retires the @p count relayed match records counted from @p position;
     * their slots are free at once. Refused when they are no longer the oldest held.
     */
    void retireMatchBatch(uint32_t position, size_t count) {
        if (position != matchRing.getHead() || count > matchRing.getStoredCount()) {
            LOG_E("FDN_RECORDS", "%u match record(s) at %u are not the oldest held; not retired",
                  static_cast<unsigned>(count), static_cast<unsigned>(position));
            return;
        }
        matchRing.retire(position, static_cast<uint32_t>(count));
        legacyStorageRefused = false;
    }

private:
    using CrashRing = FdnRecordRing<CrashRecordContent, FDN_CRASH_RECORD_CAPACITY>;
    using MatchRing = FdnRecordRing<MatchRecordContent, FDN_MATCH_RECORD_CAPACITY>;
    using LegacyMatchRing = FdnRecordRing<MatchRecordContent, FDN_LEGACY_MATCH_RECORD_CAPACITY>;

    // A frame whose kind is not its channel's is dropped: the channel fixed its
    // length, so its bytes are not a record of the kind it claims.
    bool storeCrashEntry(const FdnStoredCrashEntry& entry,
                         uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
        if (entry.kind != static_cast<uint8_t>(RecordKind::CRASH)) {
            LOG_W("FDN_RECORDS", "Dropping a record frame of kind %u on the crash channel",
                  static_cast<unsigned>(entry.kind));
            return false;
        }
        return crashRing.storeRecord(entry, recordCheck, FDN_CRASH_RECORD_CAPACITY);
    }

    bool storeMatchEntry(const FdnStoredMatchEntry& entry,
                         uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
        if (entry.kind == static_cast<uint8_t>(RecordKind::MATCH)) {
            return matchRing.storeRecord(
            entry, recordCheck, FDN_MATCH_RECORD_CAPACITY - legacyMatchRing.getStoredCount());
        }
        if (entry.kind == static_cast<uint8_t>(RecordKind::LEGACY_MATCH)) {
            return legacyMatchRing.storeRecord(
            entry, recordCheck, FDN_MATCH_RECORD_CAPACITY - matchRing.getStoredCount());
        }
        LOG_W("FDN_RECORDS", "Dropping a record frame of kind %u on the match channel",
              static_cast<unsigned>(entry.kind));
        return false;
    }

    // Set by a failed write in the crash or match ring and cleared when either
    // frees a slot: they share one partition, so room freed by one is room for
    // the other. Declared before the rings, which hold a reference to it.
    bool storageRefused = false;
    // The legacy ring's own latch. Nothing retires a legacy match, so sharing
    // the latch above would let one refused legacy write stop every current
    // match and crash record until a reboot; room the other rings free clears it.
    bool legacyStorageRefused = false;
    CrashRing crashRing;
    MatchRing matchRing;
    LegacyMatchRing legacyMatchRing;
    // After the rings they file frames into.
    FdnRecordIntake<CrashRecordTraits> crashIntake;
    FdnRecordIntake<MatchRecordTraits> matchIntake;
};
