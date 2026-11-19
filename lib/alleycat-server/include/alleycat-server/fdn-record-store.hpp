#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/crc16.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/reliable-channel.hpp"

inline constexpr const char FDN_RECORD_STORE_NAMESPACE[] = "fdn_records";
// Sized to fit the 504 usable entries of the default 20 KB NVS partition next
// to the crash log, PHY calibration and Wi-Fi: a 139-byte match entry takes 7
// NVS entries, so 27 take 189, within the 192 that 32 untagged entries took.
inline constexpr size_t FDN_MATCH_RECORD_CAPACITY = 27;
inline constexpr size_t FDN_CRASH_RECORD_CAPACITY = 16;

/// One stored record: the MAC it came from, then the frame's kind, number and
/// content. A match record's content ends with its tag, which the FDN never checks.
template <size_t CONTENT_SIZE> struct FdnStoredEntry {
    uint8_t senderMac[6];
    uint8_t kind;
    uint32_t recordNumber;
    uint8_t content[CONTENT_SIZE];
} __attribute__((packed));

// One record kind's ring of NVS slots. head and tail count records ever
// retired and ever stored; a record's slot is its count modulo CAPACITY, so
// slot keys are reused and retiring only moves the head.
template <size_t CONTENT_SIZE, size_t CAPACITY> class FdnRecordRing {
public:
    using Entry = FdnStoredEntry<CONTENT_SIZE>;

    /** Loads the ring whose storage keys start with @p keyPrefix. */
    FdnRecordRing(StorageInterface* storage, const char* keyPrefix, RecordKind kind)
        : storage(storage)
        , keyPrefix(keyPrefix)
        , kind(kind) {
        snprintf(headKey, sizeof(headKey), "%shead", keyPrefix);
        snprintf(tailKey, sizeof(tailKey), "%stail", keyPrefix);
        loadFromStorage();
    }

    /** @return Number of records held. */
    uint32_t getStoredCount() const { return tail - head; }

    /**
     * @return true when @p entry is held afterwards, so it may be acked: newly
     * stored, or byte-identical to one already held. Same sender and number
     * with different bytes is stored separately. A full ring, or an entry of
     * another kind, is refused.
     */
    bool store(const Entry& entry) {
        if (entry.kind != static_cast<uint8_t>(kind)) {
            LOG_W("FDN_RECORDS", "Dropping record %u: kind %u in the kind %u ring",
                  static_cast<unsigned>(entry.recordNumber), static_cast<unsigned>(entry.kind),
                  static_cast<unsigned>(kind));
            return false;
        }
        // Before the room check: a PDN that missed the ack resends a record already held.
        if (holds(entry)) return true;
        if (tail - head >= CAPACITY) {
            LOG_W("FDN_RECORDS", "No room for kind %u; record %u is not acked",
                  static_cast<unsigned>(kind), static_cast<unsigned>(entry.recordNumber));
            return false;
        }
        char key[16];
        slotKey(tail, key);
        // Blob, then the tail commit. An uncommitted blob sits outside
        // [head, tail) and the next store overwrites it.
        if (storage->writeBytes(FDN_RECORD_STORE_NAMESPACE, key,
                                reinterpret_cast<const uint8_t*>(&entry),
                                sizeof(entry)) != sizeof(entry) ||
            storage->writeUInt(FDN_RECORD_STORE_NAMESPACE, tailKey, tail + 1) != sizeof(uint32_t)) {
            LOG_E("FDN_RECORDS", "Storing record %u of kind %u failed",
                  static_cast<unsigned>(entry.recordNumber), static_cast<unsigned>(kind));
            return false;
        }
        rememberKey(tail, entry);
        ++tail;
        return true;
    }

    /**
     * Copies up to @p maxCount held entries into @p out, oldest first, and
     * returns how many. An unreadable entry at the head is retired with an
     * error, since it could never be uploaded and would block those behind it.
     */
    size_t peekOldest(Entry* out, size_t maxCount) {
        size_t count = 0;
        while (count < maxCount && head + count != tail) {
            if (readEntry(head + count, out[count])) {
                ++count;
                continue;
            }
            // It reaches the head once the entries before it retire.
            if (count > 0) break;
            LOG_E("FDN_RECORDS", "Retiring unreadable record %u of kind %u",
                  static_cast<unsigned>(head), static_cast<unsigned>(kind));
            retireOldest(1);
        }
        return count;
    }

    /** Retires the @p count oldest held entries. */
    void retireOldest(size_t count) {
        head += static_cast<uint32_t>(std::min<size_t>(count, tail - head));
        // A lost head write only re-offers retired records after a restart.
        if (storage->writeUInt(FDN_RECORD_STORE_NAMESPACE, headKey, head) != sizeof(uint32_t)) {
            LOG_E("FDN_RECORDS", "Storing the head of kind %u failed", static_cast<unsigned>(kind));
        }
    }

private:
    // Spares each frame a storage read per held record; the stored bytes decide.
    struct HeldKey {
        uint8_t senderMac[6];
        uint32_t recordNumber;
    };

    void slotKey(uint32_t index, char (&key)[16]) const {
        snprintf(key, sizeof(key), "%s%u", keyPrefix, static_cast<unsigned>(index % CAPACITY));
    }

    bool readEntry(uint32_t index, Entry& entry) const {
        char key[16];
        slotKey(index, key);
        return storage->readBytes(FDN_RECORD_STORE_NAMESPACE, key,
                                  reinterpret_cast<uint8_t*>(&entry),
                                  sizeof(entry)) == sizeof(entry) &&
               entry.kind == static_cast<uint8_t>(kind);
    }

    void rememberKey(uint32_t index, const Entry& entry) {
        HeldKey& held = heldKeys[index % CAPACITY];
        memcpy(held.senderMac, entry.senderMac, sizeof(held.senderMac));
        held.recordNumber = entry.recordNumber;
    }

    bool holds(const Entry& entry) const {
        for (uint32_t index = head; index != tail; ++index) {
            const HeldKey& held = heldKeys[index % CAPACITY];
            if (held.recordNumber != entry.recordNumber ||
                memcmp(held.senderMac, entry.senderMac, sizeof(held.senderMac)) != 0) {
                continue;
            }
            Entry stored{};
            if (readEntry(index, stored) && memcmp(&stored, &entry, sizeof(entry)) == 0) {
                return true;
            }
        }
        return false;
    }

    void loadFromStorage() {
        tail = storage->readUInt(FDN_RECORD_STORE_NAMESPACE, tailKey, 0);
        head = storage->readUInt(FDN_RECORD_STORE_NAMESPACE, headKey, 0);
        // Clamped so a torn head/tail pair reads as at most a full ring.
        if (head > tail) head = tail;
        if (tail - head > CAPACITY) head = tail - static_cast<uint32_t>(CAPACITY);
        Entry entry{};
        for (uint32_t index = head; index != tail; ++index) {
            if (readEntry(index, entry)) rememberKey(index, entry);
        }
    }

    StorageInterface* storage = nullptr;
    const char* keyPrefix = nullptr;
    char headKey[16] = {};
    char tailKey[16] = {};
    RecordKind kind;
    uint32_t head = 0;
    uint32_t tail = 0;
    HeldKey heldKeys[CAPACITY] = {};
};

// The FDN's NVS store of records relayed by PDNs over ESP-NOW and of its own
// crash records. It acks a relayed frame only once stored and hands records
// out oldest first. Call sync() every tick.
class FdnRecordStore {
public:
    using CrashRing = FdnRecordRing<CRASH_RECORD_SIZE, FDN_CRASH_RECORD_CAPACITY>;
    using MatchRing = FdnRecordRing<MatchRecordTraits::RECORD_SIZE, FDN_MATCH_RECORD_CAPACITY>;

    /** Loads what storage holds and claims the record and ack packet types. */
    FdnRecordStore(WirelessManager* wirelessManager, StorageInterface* storage)
        : crashRing(storage, "c_", RecordKind::CRASH)
        , matchRing(storage, "m_", RecordKind::MATCH)
        , resender(wirelessManager)
        , crashChannel(wirelessManager, &resender, PktType::kCrashRecord, nullptr)
        , matchChannel(wirelessManager, &resender, PktType::kMatchRecord, nullptr)
        // Distinct: a crash ack and a match ack to one PDN must not supersede each other.
        , ackChannel(wirelessManager, &resender, PktType::kRecordAck, nullptr,
                     Resender::SendMode::KEEP_DISTINCT) {
        crashChannel.onReceive([this](const uint8_t* fromMac, const CrashRecordFramePacket& frame) {
            onFrame(crashRing, fromMac, frame);
        });
        matchChannel.onReceive([this](const uint8_t* fromMac, const MatchRecordFramePacket& frame) {
            onFrame(matchRing, fromMac, frame);
        });
    }

    /** Pumps ack retries. */
    void sync() { resender.sync(); }

    /** The crash ring, for the uploader and for the FDN's own crash records. */
    CrashRing& getCrashRecords() { return crashRing; }

    /** The match ring, for the uploader. */
    MatchRing& getMatchRecords() { return matchRing; }

private:
    template <class Ring, size_t CONTENT_SIZE>
    void onFrame(Ring& ring, const uint8_t* fromMac, const RecordFramePacket<CONTENT_SIZE>& frame) {
        typename Ring::Entry entry{};
        memcpy(entry.senderMac, fromMac, sizeof(entry.senderMac));
        entry.kind = frame.kind;
        entry.recordNumber = frame.recordNumber;
        memcpy(entry.content, frame.content, CONTENT_SIZE);
        if (!ring.store(entry)) return;

        RecordAckPacket ack{};
        ack.kind = frame.kind;
        ack.recordNumber = frame.recordNumber;
        ack.contentCrc = crc16(frame.content, CONTENT_SIZE);
        ackChannel.sendReliable(fromMac, ack);
    }

    CrashRing crashRing;
    MatchRing matchRing;
    // Declared before the channels, which hold it and are destroyed first.
    Resender resender;
    ReliableChannel<CrashRecordFramePacket> crashChannel;
    ReliableChannel<MatchRecordFramePacket> matchChannel;
    ReliableChannel<RecordAckPacket> ackChannel;
};
