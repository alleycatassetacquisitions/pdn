#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "device/device-types.hpp"

class FdnProtocolTestSuite : public testing::Test {};

// Test: parseFdnMessage with valid Signal Echo message
void fdnProtocolParseValidMessage(FdnProtocolTestSuite* suite) {
    GameType gameType;
    KonamiButton reward;
    ASSERT_TRUE(parseFdnMessage("fdn:7:6", gameType, reward));
    ASSERT_EQ(gameType, GameType::SIGNAL_ECHO);
    ASSERT_EQ(reward, KonamiButton::START);
}

// Test: parseFdnMessage with valid Ghost Runner message
void fdnProtocolParseGhostRunner(FdnProtocolTestSuite* suite) {
    GameType gameType;
    KonamiButton reward;
    ASSERT_TRUE(parseFdnMessage("fdn:1:0", gameType, reward));
    ASSERT_EQ(gameType, GameType::GHOST_RUNNER);
    ASSERT_EQ(reward, KonamiButton::UP);
}

// Test: parseFdnMessage rejects invalid prefix
void fdnProtocolRejectInvalidPrefix(FdnProtocolTestSuite* suite) {
    GameType gameType;
    KonamiButton reward;
    ASSERT_FALSE(parseFdnMessage("cdev:7:6", gameType, reward));
    ASSERT_FALSE(parseFdnMessage("abc:7:6", gameType, reward));
}

// Test: parseFdnMessage rejects too-short message
void fdnProtocolRejectShortMessage(FdnProtocolTestSuite* suite) {
    GameType gameType;
    KonamiButton reward;
    ASSERT_FALSE(parseFdnMessage("fdn:", gameType, reward));
    ASSERT_FALSE(parseFdnMessage("fdn:7", gameType, reward));
    ASSERT_FALSE(parseFdnMessage("", gameType, reward));
}

// Test: parseFdnMessage rejects out-of-range values
void fdnProtocolRejectOutOfRange(FdnProtocolTestSuite* suite) {
    GameType gameType;
    KonamiButton reward;
    ASSERT_FALSE(parseFdnMessage("fdn:99:0", gameType, reward));
    ASSERT_FALSE(parseFdnMessage("fdn:0:99", gameType, reward));
}

// Test: parseGresMessage works correctly
void fdnProtocolParseGresMessage(FdnProtocolTestSuite* suite) {
    bool won;
    int score;
    ASSERT_TRUE(parseGresMessage("gres:1:850", won, score));
    ASSERT_TRUE(won);
    ASSERT_EQ(score, 850);

    ASSERT_TRUE(parseGresMessage("gres:0:0", won, score));
    ASSERT_FALSE(won);
    ASSERT_EQ(score, 0);
}

// Test: parseGresMessage rejects invalid messages
void fdnProtocolParseGresInvalid(FdnProtocolTestSuite* suite) {
    bool won;
    int score;
    ASSERT_FALSE(parseGresMessage("gres:", won, score));
    ASSERT_FALSE(parseGresMessage("invalid", won, score));
    ASSERT_FALSE(parseGresMessage("gres:1", won, score));
}

#endif // NATIVE_BUILD
