#pragma once

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "game/match.hpp"
#include "id-generator.hpp"

class MatchTestSuite : public testing::Test {
protected:
    void SetUp() override {
    }
};

// A UUID whose bytes are all `fill` except two zero bytes, which a string copy would stop at.
inline UUIDBytes uuidWithZeroBytes(uint8_t fill) {
    UUIDBytes id;
    memset(id.bytes, fill, sizeof(id.bytes));
    id.bytes[0] = 0;
    id.bytes[7] = 0;
    return id;
}

// Match() is defaulted, so the member initializers are the only thing that
// empties a fresh match. Nothing else in the suite reads that state.
inline void matchDefaultConstructionIsEmpty() {
    Match match;

    EXPECT_STREQ(match.getMatchId(), "");
    EXPECT_TRUE(match.getHunterPlayerId().isZero());
    EXPECT_TRUE(match.getBountyPlayerId().isZero());
    EXPECT_TRUE(match.getWinnerPlayerId().isZero());
    EXPECT_EQ(match.getHunterDrawTime(), 0UL);
    EXPECT_EQ(match.getBountyDrawTime(), 0UL);
    EXPECT_EQ(match.getOccurredAtUnixMs(), 0);
}

// A shootout id is "SHT-" then 32 digits: 36 characters with one hyphen where a
// UUID has four. uuidStringToBytes packs two hex characters per byte and skips
// hyphens, so this id yields 18 bytes. The canary catches a write past 16.
inline void matchShootoutIdConversionStaysInBounds() {
    char shootoutId[IdGenerator::UUID_BUFFER_SIZE];
    snprintf(shootoutId, sizeof(shootoutId), "SHT-%032d", 3);
    ASSERT_EQ(strlen(shootoutId), IdGenerator::UUID_STRING_LENGTH);

    struct {
        uint8_t bytes[IdGenerator::UUID_BINARY_SIZE];
        uint8_t canary[4];
    } probe;
    static_assert(sizeof(probe) == IdGenerator::UUID_BINARY_SIZE + 4,
                  "canary must sit directly after the field for this to detect anything");
    memset(&probe, 0xAA, sizeof(probe));

    IdGenerator::uuidStringToBytes(shootoutId, probe.bytes);

    for (size_t i = 0; i < sizeof(probe.canary); i++) {
        EXPECT_EQ(probe.canary[i], 0xAA) << "wrote past the uuid field at index " << i;
    }
}

// Every field, including UUIDs with zero bytes, survives serialize/deserialize.
inline void matchBinaryRoundTripPreservesAllFields() {
    Match original("12345678-1234-1234-1234-123456789abc");
    original.setPlayer(true, uuidWithZeroBytes(0x11), uuidWithZeroBytes(0x33));
    original.setPlayer(false, uuidWithZeroBytes(0x22), uuidWithZeroBytes(0x44));
    original.setWinnerPlayerId(uuidWithZeroBytes(0x22));
    original.setHunterDrawTime(150);
    original.setBountyDrawTime(275);
    original.setOccurredAtUnixMs(1763200000123LL);

    uint8_t buffer[MATCH_BINARY_SIZE];
    ASSERT_EQ(original.serialize(buffer), MATCH_BINARY_SIZE);

    Match restored;
    ASSERT_EQ(restored.deserialize(buffer), MATCH_BINARY_SIZE);

    EXPECT_STREQ(restored.getMatchId(), "12345678-1234-1234-1234-123456789abc");
    EXPECT_EQ(restored.getHunterPlayerId(), uuidWithZeroBytes(0x11));
    EXPECT_EQ(restored.getBountyPlayerId(), uuidWithZeroBytes(0x22));
    EXPECT_EQ(restored.getHunterTeamId(), uuidWithZeroBytes(0x33));
    EXPECT_EQ(restored.getBountyTeamId(), uuidWithZeroBytes(0x44));
    EXPECT_EQ(restored.getWinnerPlayerId(), uuidWithZeroBytes(0x22));
    EXPECT_EQ(restored.getHunterDrawTime(), 150UL);
    EXPECT_EQ(restored.getBountyDrawTime(), 275UL);
    EXPECT_EQ(restored.getOccurredAtUnixMs(), 1763200000123LL);
}

// The record is the packed 112-byte layout NVS stores.
inline void matchRecordLayoutIs112Bytes() {
    EXPECT_EQ(sizeof(MatchRecord), 112u);
    EXPECT_EQ(offsetof(MatchRecord, hunterDrawTimeMs), 16u);
    EXPECT_EQ(offsetof(MatchRecord, hunterPlayerId), 24u);
    EXPECT_EQ(offsetof(MatchRecord, winnerPlayerId), 88u);
    EXPECT_EQ(offsetof(MatchRecord, occurredAtUnixMs), 104u);
}

inline void matchSetupClearsDrawTimes() {
    Match match;
    match.setHunterDrawTime(500);
    match.setBountyDrawTime(600);

    // Re-creating the match should reset draw times
    match = Match("new-match");

    EXPECT_EQ(match.getHunterDrawTime(), 0);
    EXPECT_EQ(match.getBountyDrawTime(), 0);
    EXPECT_STREQ(match.getMatchId(), "new-match");
}

inline void matchDrawTimesSetCorrectly() {
    Match match("match");

    match.setHunterDrawTime(123);
    EXPECT_EQ(match.getHunterDrawTime(), 123);

    match.setBountyDrawTime(456);
    EXPECT_EQ(match.getBountyDrawTime(), 456);
}

inline void matchWithLargeDrawTimes() {
    Match match("match");

    unsigned long largeTime = 999999999UL;
    match.setHunterDrawTime(largeTime);
    match.setBountyDrawTime(largeTime - 1);

    uint8_t buffer[MATCH_BINARY_SIZE];
    match.serialize(buffer);
    Match restored;
    restored.deserialize(buffer);

    EXPECT_EQ(restored.getHunterDrawTime(), largeTime);
    EXPECT_EQ(restored.getBountyDrawTime(), largeTime - 1);
}
