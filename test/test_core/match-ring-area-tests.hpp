#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include "device/drivers/native/native-prefs-driver.hpp"
#include "game/match-manager.hpp"
#include "match-manager-tests.hpp"

// ============================================
// The match ring in its own storage area
// ============================================

/**
 * In-memory storage whose blob writes fail once blobWritesLeft runs out, as an
 * area that fills up, or loses power, part way through a move would.
 */
class BlobBudgetPrefs : public NativePrefsDriver {
public:
    /** Registers @p namespaces and allows @p blobWrites blob writes. */
    BlobBudgetPrefs(const std::string& name, std::initializer_list<const char*> namespaces,
                    int blobWrites)
        : NativePrefsDriver(name, namespaces)
        , blobWritesLeft(blobWrites) {}

    /** Fails, writing nothing, once the budget is spent. */
    size_t writeBytes(const std::string& ns, const std::string& key, const uint8_t* data,
                      size_t length) override {
        if (blobWritesLeft == 0) {
            return 0;
        }
        blobWritesLeft--;
        return NativePrefsDriver::writeBytes(ns, key, data, length);
    }

    int blobWritesLeft;
};

// A ring record whose bytes all say `number`, so each is told apart from the others.
inline MatchRecordContent numberedRingRecord(uint32_t number) {
    MatchRecordContent content{};
    memset(&content, static_cast<int>(number), sizeof(content));
    return content;
}

inline void ringSlotKey(uint32_t number, char (&key)[16]) {
    snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX,
             static_cast<unsigned>((number - 1) % MATCH_RING_CAPACITY));
}

// Stores in @p area the ring records @p first to @p newest as this firmware
// does, numberedRingRecord(n) as record n, with @p collected as the mark.
inline void writeRing(StorageInterface& area, uint32_t first, uint32_t newest, uint32_t collected) {
    for (uint32_t number = first; number <= newest; number++) {
        char key[16];
        ringSlotKey(number, key);
        const MatchRecordContent content = numberedRingRecord(number);
        area.writeBytes(MATCH_RING_PREFS_NAMESPACE, key, reinterpret_cast<const uint8_t*>(&content),
                        MATCH_SLOT_SIZE);
    }
    area.writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, newest);
    area.writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, collected);
}

inline bool sameRecord(const MatchRecordContent& left, const MatchRecordContent& right) {
    return memcmp(&left, &right, sizeof(left)) == 0;
}

// Reads @p area's ring slot for record @p number; false unless it holds a tagged record.
inline bool readRingSlotIn(StorageInterface& area, uint32_t number, MatchRecordContent& held) {
    char key[16];
    ringSlotKey(number, key);
    return area.readBytes(MATCH_RING_PREFS_NAMESPACE, key, reinterpret_cast<uint8_t*>(&held),
                          sizeof(held)) == MATCH_SLOT_SIZE;
}

// True when @p area's ring slot for record @p number holds @p expected.
inline bool ringSlotHolds(StorageInterface& area, uint32_t number,
                          const MatchRecordContent& expected) {
    MatchRecordContent held{};
    return readRingSlotIn(area, number, held) && sameRecord(held, expected);
}

// True when @p area holds no key of the ring: no slot, no "seq", no "collectedSeq".
inline bool ringIsGone(StorageInterface& area) {
    constexpr uint32_t ABSENT = 0xA65E47u;
    for (uint32_t number = 1; number <= MATCH_RING_CAPACITY; number++) {
        char key[16];
        ringSlotKey(number, key);
        uint8_t slot[MATCH_SLOT_SIZE] = {};
        if (area.readBytes(MATCH_RING_PREFS_NAMESPACE, key, slot, sizeof(slot)) != 0) {
            return false;
        }
    }
    return area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, ABSENT) == ABSENT &&
           area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, ABSENT) == ABSENT;
}

inline NativePrefsDriver& makeRingArea(MatchManagerTestSuite* suite) {
    auto* area = new NativePrefsDriver("ring-area", {MATCH_RING_PREFS_NAMESPACE});
    suite->ringArea = area;
    return *area;
}

// The first boot with the ring's own area: its pending records move there with
// their numbers, the collected ones are not carried, and the default partition
// keeps nothing of the ring.
TEST_F(MatchManagerTestSuite, ringRecordsMoveToTheirOwnAreaAtBoot) {
    NativePrefsDriver& prefs = makeNativePrefs("move-prefs");
    writeRing(prefs, 1, 5, 2);
    NativePrefsDriver& area = makeRingArea(this);

    rebuildMatchManager(&prefs, &area);

    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 5u);
    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0), 2u)
    << "the moved records must keep their numbers";
    for (uint32_t number = 3; number <= 5; number++) {
        EXPECT_TRUE(ringSlotHolds(area, number, numberedRingRecord(number))) << "match " << number;
    }
    EXPECT_FALSE(ringSlotHolds(area, 1, numberedRingRecord(1))) << "a collected match was moved";
    EXPECT_TRUE(ringIsGone(prefs)) << "the default partition still holds ring entries";

    uint32_t recordNumber = 0;
    MatchRecordContent content{};
    ASSERT_TRUE(readLowestPending(recordNumber, content));
    EXPECT_EQ(recordNumber, 3u);
    EXPECT_TRUE(sameRecord(content, numberedRingRecord(3)));
}

// The ring's area did not open: the ring is read and written in the default
// partition, as before it had an area.
TEST_F(MatchManagerTestSuite, ringStaysInDefaultPartitionWithoutItsArea) {
    NativePrefsDriver& prefs = makeNativePrefs("fallback-prefs");
    writeRing(prefs, 1, 2, 0);

    rebuildMatchManager(&prefs, nullptr);

    uint32_t recordNumber = 0;
    MatchRecordContent content{};
    ASSERT_TRUE(readLowestPending(recordNumber, content));
    EXPECT_EQ(recordNumber, 1u);
    setupMatchAsBounty("0e0e0e0e-2222-4000-8000-000000000003");
    finishMatchWithDrawTimes(180, 120);
    EXPECT_EQ(prefs.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 3u);
    EXPECT_TRUE(ringSlotHolds(prefs, 1, numberedRingRecord(1)));
}

// A match played with the ring in its area, stored and acknowledged, takes no
// entry of the default partition, which older firmware's matches may have filled.
TEST_F(MatchManagerTestSuite, ringInItsAreaTakesNoDefaultPartitionEntries) {
    NativePrefsDriver& prefs = makeNativePrefs("area-only-prefs");
    NativePrefsDriver& area = makeRingArea(this);
    rebuildMatchManager(&prefs, &area);

    setupMatchAsBounty("0e0e0e0e-3333-4000-8000-000000000001");
    finishMatchWithDrawTimes(180, 120);
    ASSERT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 1u);
    MatchRecordContent stored{};
    ASSERT_TRUE(readRingSlotIn(area, 1, stored));
    uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(stored, recordCheck);
    matchManager->onRecordAcknowledged(RecordKind::MATCH, 1, recordCheck);

    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0), 1u);
    EXPECT_TRUE(ringIsGone(prefs)) << "the ring wrote to the default partition";
}

// Older firmware's matches stay in the default partition and are sent as
// before, after the ring's own: its legacy ring, then its JSON matches, each
// erased there on its acknowledgement.
TEST_F(MatchManagerTestSuite, olderFirmwareMatchesStayInDefaultPartitionAndAreSent) {
    NativePrefsDriver& prefs = makeNativePrefs("legacy-area-prefs");
    const MatchRecordContent legacySlot = numberedRingRecord(0x51);
    prefs.writeBytes(LEGACY_MATCHES_PREFS_NAMESPACE, "match_0",
                     reinterpret_cast<const uint8_t*>(&legacySlot), LEGACY_UNTAGGED_SLOT_SIZE);
    prefs.writeUInt(LEGACY_MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, 1);
    const std::string json =
    R"({"match_id":"0e0e0e0e-0000-4000-8000-000000000007","hunter":"1234","bounty":"5678",)"
    R"("winner_is_hunter":true,"hunter_time":180,"bounty_time":240})";
    prefs.write(LEGACY_MATCHES_PREFS_NAMESPACE, "match_7", json);
    prefs.writeUChar(LEGACY_MATCHES_PREFS_NAMESPACE, LEGACY_PREF_COUNT_KEY, 8);
    writeRing(prefs, 1, 1, 0);
    NativePrefsDriver& area = makeRingArea(this);
    rebuildMatchManager(&prefs, &area);

    uint32_t recordNumber = 0;
    RecordKind kind = RecordKind::CRASH;
    MatchRecordContent content{};
    uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
    ASSERT_TRUE(matchManager->readLowestPendingFrame(recordNumber, kind, content));
    EXPECT_TRUE(sameRecord(content, numberedRingRecord(1))) << "the moved ring record goes first";
    computeRecordCheck(content, recordCheck);
    matchManager->onRecordAcknowledged(kind, recordNumber, recordCheck);

    ASSERT_TRUE(matchManager->readLowestPendingFrame(recordNumber, kind, content));
    EXPECT_EQ(kind, RecordKind::MATCH);
    EXPECT_EQ(memcmp(content.recordBytes, legacySlot.recordBytes, MATCH_RECORD_SIZE), 0)
    << "older firmware's ring slot is sent from the default partition";
    computeRecordCheck(content, recordCheck);
    matchManager->onRecordAcknowledged(kind, recordNumber, recordCheck);
    EXPECT_EQ(prefs.readUInt(LEGACY_MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0), 1u);

    ASSERT_TRUE(matchManager->readLowestPendingFrame(recordNumber, kind, content));
    EXPECT_EQ(kind, RecordKind::LEGACY_MATCH);
    EXPECT_EQ(recordNumber, 7u);
    computeRecordCheck(content, recordCheck);
    matchManager->onRecordAcknowledged(kind, recordNumber, recordCheck);
    EXPECT_EQ(prefs.read(LEGACY_MATCHES_PREFS_NAMESPACE, "match_7", ""), "");
    EXPECT_FALSE(matchManager->readLowestPendingFrame(recordNumber, kind, content));
}

// Every boot after the move finds nothing to move and leaves the ring as it is.
TEST_F(MatchManagerTestSuite, rebootAfterRingMoveChangesNothing) {
    NativePrefsDriver& prefs = makeNativePrefs("reboot-prefs");
    writeRing(prefs, 1, 4, 1);
    NativePrefsDriver& area = makeRingArea(this);
    rebuildMatchManager(&prefs, &area);

    rebuildMatchManager(&prefs, &area);

    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 4u);
    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0), 1u);
    for (uint32_t number = 2; number <= 4; number++) {
        EXPECT_TRUE(ringSlotHolds(area, number, numberedRingRecord(number))) << "match " << number;
    }
    EXPECT_TRUE(ringIsGone(prefs));
}

// A move cut short leaves every record in at least one place; the next boot
// finishes it without holding a record twice.
TEST_F(MatchManagerTestSuite, ringMoveCutShortLosesNothingAndFinishesNextBoot) {
    NativePrefsDriver& prefs = makeNativePrefs("cut-prefs");
    writeRing(prefs, 1, 5, 0);
    auto* area = new BlobBudgetPrefs("cut-area", {MATCH_RING_PREFS_NAMESPACE}, 2);
    ringArea = area;

    rebuildMatchManager(&prefs, area);

    for (uint32_t number = 1; number <= 5; number++) {
        EXPECT_TRUE(ringSlotHolds(prefs, number, numberedRingRecord(number)) ||
                    ringSlotHolds(*area, number, numberedRingRecord(number)))
        << "match " << number << " is in neither place";
    }
    EXPECT_TRUE(ringSlotHolds(prefs, 3, numberedRingRecord(3)))
    << "a match not yet held in the area was erased";

    area->blobWritesLeft = 100;
    rebuildMatchManager(&prefs, area);

    EXPECT_EQ(area->readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 5u)
    << "a match moved before the cut was stored again";
    for (uint32_t number = 1; number <= 5; number++) {
        EXPECT_TRUE(ringSlotHolds(*area, number, numberedRingRecord(number))) << "match " << number;
    }
    EXPECT_TRUE(ringIsGone(prefs));
}

// Power lost while the default partition's ring was being erased: its "seq" is
// still there, and the next boot erases the rest without storing anything again.
TEST_F(MatchManagerTestSuite, ringEraseCutShortFinishesNextBoot) {
    NativePrefsDriver& prefs = makeNativePrefs("erase-cut-prefs");
    writeRing(prefs, 1, 3, 0);
    NativePrefsDriver& area = makeRingArea(this);
    writeRing(area, 1, 3, 0);
    prefs.remove(MATCH_RING_PREFS_NAMESPACE, "match_0");

    rebuildMatchManager(&prefs, &area);

    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 3u);
    EXPECT_TRUE(ringIsGone(prefs));
}

// The area already holds a ring, as after a boot on which it did not open and
// new matches went to the default partition: those are added after it.
TEST_F(MatchManagerTestSuite, ringRecordsMoveAfterTheRingTheAreaHolds) {
    NativePrefsDriver& prefs = makeNativePrefs("merge-prefs");
    writeRing(prefs, 1, 2, 0);
    NativePrefsDriver& area = makeRingArea(this);
    writeRing(area, 3, 7, 5);

    rebuildMatchManager(&prefs, &area);

    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 9u);
    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0), 5u);
    EXPECT_TRUE(ringSlotHolds(area, 8, numberedRingRecord(1)));
    EXPECT_TRUE(ringSlotHolds(area, 9, numberedRingRecord(2)));
    EXPECT_TRUE(ringIsGone(prefs));
}

// A record that cannot be read could never be sent: it is dropped, and the
// records after it still move.
TEST_F(MatchManagerTestSuite, unreadableRingRecordIsNotMovedAndHoldsNothingBack) {
    NativePrefsDriver& prefs = makeNativePrefs("torn-move-prefs");
    writeRing(prefs, 1, 3, 0);
    const uint8_t torn[3] = {1, 2, 3};
    prefs.writeBytes(MATCH_RING_PREFS_NAMESPACE, "match_1", torn, sizeof(torn));
    NativePrefsDriver& area = makeRingArea(this);

    rebuildMatchManager(&prefs, &area);

    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0), 2u);
    EXPECT_TRUE(ringSlotHolds(area, 1, numberedRingRecord(1)));
    EXPECT_TRUE(ringSlotHolds(area, 2, numberedRingRecord(3)));
    EXPECT_TRUE(ringIsGone(prefs));
}

// The clamp of a mark above the newest record applies to the ring where it is,
// in its area.
TEST_F(MatchManagerTestSuite, collectedMarkAboveNewestIsClampedInTheRingArea) {
    NativePrefsDriver& prefs = makeNativePrefs("clamp-area-prefs");
    NativePrefsDriver& area = makeRingArea(this);
    area.writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 5u);
    area.writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 9u);

    rebuildMatchManager(&prefs, &area);

    EXPECT_EQ(area.readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0), 5u);
}
