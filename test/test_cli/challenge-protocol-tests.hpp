//
// Challenge Protocol Tests — device-types.hpp parsing and lookup functions
//

#pragma once

#include <gtest/gtest.h>
#include "device/device-types.hpp"
#include "device/device-constants.hpp"

// ============================================
// CHALLENGE PROTOCOL TEST SUITE
// ============================================

class ChallengeProtocolTestSuite : public testing::Test {
public:
    // No setup needed — all functions under test are stateless
};

// Test: Parse a valid "cdev:7:6" message
void protocolParseCdevMessage(ChallengeProtocolTestSuite* /*suite*/) {
    GameType gameType;
    KonamiButton reward;
    bool ok = parseCdevMessage("cdev:7:6", gameType, reward);

    ASSERT_TRUE(ok);
    ASSERT_EQ(gameType, GameType::SIGNAL_ECHO);
    ASSERT_EQ(reward, KonamiButton::START);
}

// Test: Malformed "cdev:" messages return false
void protocolParseCdevInvalid(ChallengeProtocolTestSuite* /*suite*/) {
    GameType gameType;
    KonamiButton reward;

    ASSERT_FALSE(parseCdevMessage("", gameType, reward));
    ASSERT_FALSE(parseCdevMessage("cdev:", gameType, reward));
    ASSERT_FALSE(parseCdevMessage("cdev:99:99", gameType, reward));
    ASSERT_FALSE(parseCdevMessage("heartbeat", gameType, reward));
    ASSERT_FALSE(parseCdevMessage("cdev:abc:def", gameType, reward));
}

// Test: Parse a valid "gres:1:850" message
void protocolParseGresMessage(ChallengeProtocolTestSuite* /*suite*/) {
    bool won;
    int score;
    bool ok = parseGresMessage("gres:1:850", won, score);

    ASSERT_TRUE(ok);
    ASSERT_TRUE(won);
    ASSERT_EQ(score, 850);

    ok = parseGresMessage("gres:0:0", won, score);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(won);
    ASSERT_EQ(score, 0);
}

// Test: Game type lookup functions return correct values
void protocolGameTypeLookup(ChallengeProtocolTestSuite* /*suite*/) {
    ASSERT_STREQ(getGameDisplayName(GameType::SIGNAL_ECHO), "SIGNAL ECHO");
    ASSERT_STREQ(getGameDisplayName(GameType::GHOST_RUNNER), "GHOST RUNNER");
    ASSERT_STREQ(getGameDisplayName(GameType::QUICKDRAW), "QUICKDRAW");

    ASSERT_EQ(getRewardForGame(GameType::SIGNAL_ECHO), KonamiButton::START);
    ASSERT_EQ(getRewardForGame(GameType::GHOST_RUNNER), KonamiButton::UP);
    ASSERT_EQ(getRewardForGame(GameType::BREACH_DEFENSE), KonamiButton::A);

    ASSERT_STREQ(getKonamiButtonName(KonamiButton::START), "START");
    ASSERT_STREQ(getKonamiButtonName(KonamiButton::UP), "UP");
}

// Test: isChallengeDeviceCode("7007") returns true
void magicCodeDetectsChallenge(ChallengeProtocolTestSuite* /*suite*/) {
    ASSERT_TRUE(isChallengeDeviceCode("7001"));
    ASSERT_TRUE(isChallengeDeviceCode("7002"));
    ASSERT_TRUE(isChallengeDeviceCode("7003"));
    ASSERT_TRUE(isChallengeDeviceCode("7004"));
    ASSERT_TRUE(isChallengeDeviceCode("7005"));
    ASSERT_TRUE(isChallengeDeviceCode("7006"));
    ASSERT_TRUE(isChallengeDeviceCode("7007"));
}

// Test: isChallengeDeviceCode("0010") returns false
void magicCodeRejectsPlayer(ChallengeProtocolTestSuite* /*suite*/) {
    ASSERT_FALSE(isChallengeDeviceCode("0010"));
    ASSERT_FALSE(isChallengeDeviceCode("9999"));
    ASSERT_FALSE(isChallengeDeviceCode("8888"));
    ASSERT_FALSE(isChallengeDeviceCode("1111"));
    ASSERT_FALSE(isChallengeDeviceCode("6969"));
    ASSERT_FALSE(isChallengeDeviceCode("7000"));
    ASSERT_FALSE(isChallengeDeviceCode("7008"));
    ASSERT_FALSE(isChallengeDeviceCode(""));
}

// Test: getGameTypeFromCode("7007") returns SIGNAL_ECHO
void magicCodeMapsToGameType(ChallengeProtocolTestSuite* /*suite*/) {
    ASSERT_EQ(getGameTypeFromCode("7007"), GameType::SIGNAL_ECHO);
    ASSERT_EQ(getGameTypeFromCode("7001"), GameType::GHOST_RUNNER);
    ASSERT_EQ(getGameTypeFromCode("7003"), GameType::FIREWALL_DECRYPT);
    ASSERT_EQ(getGameTypeFromCode("0010"), GameType::QUICKDRAW);  // Not a challenge code
    ASSERT_EQ(getGameTypeFromCode("9999"), GameType::QUICKDRAW);  // Not a challenge code
}
