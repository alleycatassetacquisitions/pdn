#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "device/drivers/native/native-prefs-driver.hpp"
#include "game/match-manager.hpp"
#include "match-manager-tests.hpp"

// ============================================
// The match ring in its own storage area
// ============================================

// A ring area that does not open, as when its range cannot be registered.
class UnopenableRingArea : public NativePrefsDriver {
public:
    using NativePrefsDriver::NativePrefsDriver;
    /** Refuses, as the ESP32 driver does when its partition does not come up. */
    int initialize() override { return 1; }
};

// Fails the next collectedMarkFailures writes of "collectedSeq", as power lost
// just after a record moved would leave the ring it moved from.
class CollectedMarkFailingPrefs : public NativePrefsDriver {
public:
    using NativePrefsDriver::NativePrefsDriver;
    /** Fails a "collectedSeq" write while collectedMarkFailures counts down. */
    size_t writeUInt(const std::string& ns, const std::string& key, uint32_t value) override {
        if (key == "collectedSeq" && collectedMarkFailures > 0) {
            collectedMarkFailures--;
            return 0;
        }
        return NativePrefsDriver::writeUInt(ns, key, value);
    }
    int collectedMarkFailures = 0;
};

// A tagged ring record whose 128 bytes all say `number`.
inline TaggedMatchRecord numberedRingRecord(uint32_t number) {
    TaggedMatchRecord record{};
    memset(&record, static_cast<int>(number), sizeof(record));
    return record;
}

inline std::string ringSlotKey(uint32_t number) {
    return "match_" + std::to_string((number - 1) % MatchManager::MATCH_RING_CAPACITY);
}

// Stores records @p first..@p newest in @p area as a ring with mark @p collected.
inline void writeRing(StorageInterface& area, uint32_t first, uint32_t newest, uint32_t collected) {
    for (uint32_t number = first; number <= newest; number++) {
        const TaggedMatchRecord record = numberedRingRecord(number);
        area.writeBytes(MATCH_RING_PREFS_NAMESPACE, ringSlotKey(number),
                        reinterpret_cast<const uint8_t*>(&record), sizeof(record));
    }
    area.writeUInt(MATCH_RING_PREFS_NAMESPACE, "seq", newest);
    area.writeUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", collected);
}

// True when the manager's ring holds, as record @p number, all 128 bytes of @p expected.
inline bool ringHolds(MatchManager* matchManager, uint32_t number,
                      const TaggedMatchRecord& expected) {
    TaggedMatchRecord held{};
    return matchManager->readRecord(number, held) && memcmp(&held, &expected, sizeof(held)) == 0;
}

// True when @p area keeps no slot, "seq" or "collectedSeq" of the ring.
inline bool ringIsGone(StorageInterface& area) {
    constexpr uint32_t ABSENT = 0xA65E47u;
    for (uint32_t number = 1; number <= MatchManager::MATCH_RING_CAPACITY; number++) {
        uint8_t slot[sizeof(TaggedMatchRecord)] = {};
        if (area.readBytes(MATCH_RING_PREFS_NAMESPACE, ringSlotKey(number), slot, sizeof(slot)) !=
            0) {
            return false;
        }
    }
    return area.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", ABSENT) == ABSENT &&
           area.readUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", ABSENT) == ABSENT;
}

// Older firmware's matches as key and value pairs in the "matches" namespace.
using OlderFirmwareMatches = std::vector<std::pair<std::string, std::string>>;

// Writes older firmware's matches into @p storage's "matches" namespace.
inline OlderFirmwareMatches writeOlderFirmwareMatches(StorageInterface& storage) {
    const OlderFirmwareMatches legacy = {
    {"count", "2"},
    {"match_0", R"({"match_id":"a","hunter":"1111","bounty":"2222"})"},
    {"match_1", R"({"match_id":"b","hunter":"3333","bounty":"4444"})"},
    };
    for (const std::pair<std::string, std::string>& entry : legacy) {
        storage.write(LEGACY_MATCHES_NAMESPACE, entry.first, entry.second);
    }
    return legacy;
}

inline void expectOlderFirmwareMatchesUntouched(StorageInterface& storage,
                                                const OlderFirmwareMatches& legacy) {
    for (const std::pair<std::string, std::string>& entry : legacy) {
        EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, entry.first, ""), entry.second);
    }
    EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, "match_2", "absent"), "absent");
}

// The match manager suite with an area for the ring, alive until TearDown ends it.
class MatchRingAreaTests : public MatchManagerTestSuite {
public:
    NativePrefsDriver area{"ring-area", {MATCH_RING_PREFS_NAMESPACE}};
};

// The first boot with the area: pending records move with their numbers and
// all their bytes, collected ones stay behind and go with the old ring.
TEST_F(MatchRingAreaTests, ringRecordsMoveToTheirOwnAreaAtBoot) {
    writeRing(storage, 1, 5, 2);
    const OlderFirmwareMatches legacy = writeOlderFirmwareMatches(storage);

    matchManager->initialize(player, &storage, &area);

    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 5u);
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 2u);
    for (uint32_t number = 3; number <= 5; number++) {
        EXPECT_TRUE(ringHolds(matchManager, number, numberedRingRecord(number))) << number;
    }
    EXPECT_FALSE(ringHolds(matchManager, 2, numberedRingRecord(2))) << "a collected match moved";
    EXPECT_TRUE(ringIsGone(storage));
    expectOlderFirmwareMatchesUntouched(storage, legacy);

    // A later boot finds nothing to move.
    matchManager->initialize(player, &storage, &area);
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 5u);
    EXPECT_TRUE(ringHolds(matchManager, 5, numberedRingRecord(5)));
}

// A ring that has wrapped holds only its newest 32 records; all 32 move with
// their numbers and every slot of the old ring is erased.
TEST_F(MatchRingAreaTests, wrappedRingMovesItsNewest32WithTheirNumbers) {
    writeRing(storage, 1, 40, 0);

    matchManager->initialize(player, &storage, &area);

    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 40u);
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 8u);
    for (uint32_t number = 9; number <= 40; number++) {
        EXPECT_TRUE(ringHolds(matchManager, number, numberedRingRecord(number))) << number;
    }
    EXPECT_TRUE(ringIsGone(storage));
}

// The area did not open: the ring is read and written where it was.
TEST_F(MatchRingAreaTests, ringStaysInStorageWhenItsAreaDoesNotOpen) {
    writeRing(storage, 1, 2, 0);
    UnopenableRingArea unopenable{"ring-area", {MATCH_RING_PREFS_NAMESPACE}};

    matchManager->initialize(player, &storage, &unopenable);
    ASSERT_TRUE(playHunterWin());

    EXPECT_EQ(storage.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0), 3u);
    EXPECT_TRUE(ringHolds(matchManager, 1, numberedRingRecord(1)));
    EXPECT_EQ(unopenable.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0), 0u);
}

// A full area refuses both moved records and new matches; the records that
// could not move wait where they were and follow the area's ring once it has room.
TEST_F(MatchRingAreaTests, fullRingInItsAreaRefusesAndTheRestWait) {
    writeRing(area, 1, MatchManager::MATCH_RING_CAPACITY, 0);
    writeRing(storage, 101, 102, 100);

    matchManager->initialize(player, &storage, &area);
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 32u);
    EXPECT_FALSE(playHunterWin()) << "a match was stored over a pending one";
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 32u);
    EXPECT_EQ(storage.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0), 102u);
    EXPECT_EQ(storage.readUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", 0), 100u);

    ASSERT_TRUE(matchManager->markCollected(32));
    matchManager->initialize(player, &storage, &area);
    EXPECT_TRUE(ringHolds(matchManager, 33, numberedRingRecord(101)));
    EXPECT_TRUE(ringHolds(matchManager, 34, numberedRingRecord(102)));
    EXPECT_TRUE(ringIsGone(storage));
}

// Power lost after a record moved but before it was marked there: the next
// boot finishes the move and holds that record once.
TEST_F(MatchRingAreaTests, ringMoveCutShortFinishesWithoutARecordTwice) {
    CollectedMarkFailingPrefs previous{"previous",
                                       {MATCH_RING_PREFS_NAMESPACE, LEGACY_MATCHES_NAMESPACE}};
    writeRing(previous, 1, 3, 0);
    const OlderFirmwareMatches legacy = writeOlderFirmwareMatches(previous);
    previous.collectedMarkFailures = 1;

    matchManager->initialize(player, &previous, &area);
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    ASSERT_EQ(previous.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0), 3u);

    matchManager->initialize(player, &previous, &area);
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 3u) << "a record was stored twice";
    for (uint32_t number = 1; number <= 3; number++) {
        EXPECT_TRUE(ringHolds(matchManager, number, numberedRingRecord(number))) << number;
    }
    EXPECT_TRUE(ringIsGone(previous));
    expectOlderFirmwareMatchesUntouched(previous, legacy);
}

// A slot that cannot be read is not moved and holds back none after it.
TEST_F(MatchRingAreaTests, unreadableRingRecordIsNotMovedAndHoldsNothingBack) {
    writeRing(storage, 1, 3, 0);
    const uint8_t untagged[MATCH_BINARY_SIZE] = {};
    storage.writeBytes(MATCH_RING_PREFS_NAMESPACE, ringSlotKey(2), untagged, sizeof(untagged));

    matchManager->initialize(player, &storage, &area);

    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 2u);
    EXPECT_TRUE(ringHolds(matchManager, 1, numberedRingRecord(1)));
    EXPECT_TRUE(ringHolds(matchManager, 2, numberedRingRecord(3)));
    EXPECT_TRUE(ringIsGone(storage));
}
