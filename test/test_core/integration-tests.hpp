#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "../test-constants.hpp"

// ============================================
// Integration Test Fixture
// ============================================

/**
 * Integration tests simulate two-device scenarios by:
 * 1. Setting up one device's perspective
 * 2. Serializing match data (simulating network transmission)
 * 3. Resetting and setting up the other device's perspective
 * 4. Deserializing and verifying
 */
class DuelIntegrationTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Seed ID generator for reproducible tests
        IdGenerator idGenerator = IdGenerator(TestConstants::TEST_SEED_INTEGRATION);
        
        // Create two players (simulating two different devices)
        hunter = new Player();
        char hunterId[] = "a0b1c2d3-1234-5678-9abc-def012345678";
        hunter->setUserID(hunterId);
        hunter->setName("HunterPlayer");
        hunter->setIsHunter(true);

        bounty = new Player();
        char bountyId[] = "b0a1b2c3-1234-5678-9abc-def012345678";
        bounty->setUserID(bountyId);
        bounty->setName("BountyPlayer");
        bounty->setIsHunter(false);

        // Create MatchManager instance
        matchManager = new MatchManager();
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete hunter;
        delete bounty;
    }

    // Helper: Initialize match manager for hunter's device
    void initializeAsHunterDevice() {
        matchManager->clearCurrentMatch();
        matchManager->initialize(hunter, &hunterStorage, &hunterPeerComms, &hunterWirelessManager);
    }

    // Helper: Initialize match manager for bounty's device
    void initializeAsBountyDevice() {
        matchManager->clearCurrentMatch();
        matchManager->initialize(bounty, &bountyStorage, &bountyPeerComms, &bountyWirelessManager);
    }

    Player* hunter;
    Player* bounty;
    MatchManager* matchManager;
    
    // Hunter device mocks
    MockStorage hunterStorage;
    MockPeerComms hunterPeerComms;
    MockQuickdrawWirelessManager hunterWirelessManager;
    
    // Bounty device mocks
    MockStorage bountyStorage;
    MockPeerComms bountyPeerComms;
    MockQuickdrawWirelessManager bountyWirelessManager;
};

// ============================================
// Complete Duel Flow: Two Device Simulation - Hunter Wins
// ============================================

inline void completeDuelFlowHunterWins(DuelIntegrationTestSuite* suite) {
    const unsigned long HUNTER_REACTION_MS = 200;
    const unsigned long BOUNTY_REACTION_MS = 300;
    
    // Generate match ID
    char* matchId = IdGenerator(TestConstants::TEST_SEED_INTEGRATION).generateId();
    std::string matchIdStr(matchId);
    
    // ========== HUNTER'S DEVICE ==========
    suite->initializeAsHunterDevice();
    
    // Hunter creates the match
    Match* hunterMatch = suite->matchManager->createMatch(
        matchIdStr, 
        suite->hunter->getUserID(), 
        suite->bounty->getUserID()
    );
    ASSERT_NE(hunterMatch, nullptr);

    // Duel countdown completes
    suite->matchManager->setDuelLocalStartTime(10000);

    // Hunter presses button (200ms reaction)
    suite->matchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->matchManager->setReceivedButtonPush();

    // Serialize match data to send to bounty
    uint8_t matchBuffer[MATCH_BINARY_SIZE];
    hunterMatch->serialize(matchBuffer);

    // Hunter receives bounty's result (via network)
    suite->matchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->matchManager->setReceivedDrawResult();

    // Verify hunter's perspective
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin()); // Hunter should win
    
    // Update hunter's stats
    suite->hunter->incrementWins();
    suite->hunter->incrementMatchesPlayed();
    suite->hunter->incrementStreak();
    suite->hunter->addReactionTime(HUNTER_REACTION_MS);

    EXPECT_EQ(suite->hunter->getWins(), 1);
    EXPECT_EQ(suite->hunter->getStreak(), 1);

    // ========== BOUNTY'S DEVICE ==========
    suite->initializeAsBountyDevice();
    
    // Bounty receives match data from hunter
    Match receivedMatch;
    receivedMatch.deserialize(matchBuffer);
    Match* bountyMatch = suite->matchManager->receiveMatch(receivedMatch);
    ASSERT_NE(bountyMatch, nullptr);

    suite->matchManager->setDuelLocalStartTime(10000);

    // Bounty presses button (300ms reaction - slower)
    suite->matchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->matchManager->setReceivedButtonPush();

    // Bounty receives hunter's result (via network)
    suite->matchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->matchManager->setReceivedDrawResult();

    // Verify bounty's perspective
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin()); // Bounty should lose

    // Update bounty's stats
    suite->bounty->incrementLosses();
    suite->bounty->incrementMatchesPlayed();
    suite->bounty->resetStreak();
    suite->bounty->addReactionTime(BOUNTY_REACTION_MS);

    EXPECT_EQ(suite->bounty->getLosses(), 1);
    EXPECT_EQ(suite->bounty->getStreak(), 0);
}

// ============================================
// Complete Duel Flow: Two Device Simulation - Bounty Wins
// ============================================

inline void completeDuelFlowBountyWins(DuelIntegrationTestSuite* suite) {
    const unsigned long HUNTER_REACTION_MS = 350;
    const unsigned long BOUNTY_REACTION_MS = 180;
    
    char* matchId = IdGenerator(TestConstants::TEST_SEED_INTEGRATION).generateId();
    std::string matchIdStr(matchId);
    
    // ========== HUNTER'S DEVICE ==========
    suite->initializeAsHunterDevice();
    
    Match* hunterMatch = suite->matchManager->createMatch(
        matchIdStr, 
        suite->hunter->getUserID(), 
        suite->bounty->getUserID()
    );
    ASSERT_NE(hunterMatch, nullptr);

    suite->matchManager->setDuelLocalStartTime(5000);
    suite->matchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->matchManager->setReceivedButtonPush();

    // Serialize for bounty
    uint8_t matchBuffer[MATCH_BINARY_SIZE];
    hunterMatch->serialize(matchBuffer);

    // Receive bounty's faster result
    suite->matchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->matchManager->setReceivedDrawResult();

    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin()); // Hunter loses

    // ========== BOUNTY'S DEVICE ==========
    suite->initializeAsBountyDevice();
    
    Match receivedMatch;
    receivedMatch.deserialize(matchBuffer);
    Match* bountyMatch = suite->matchManager->receiveMatch(receivedMatch);
    ASSERT_NE(bountyMatch, nullptr);

    suite->matchManager->setDuelLocalStartTime(5000);
    suite->matchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->matchManager->setReceivedButtonPush();

    suite->matchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->matchManager->setReceivedDrawResult();

    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin()); // Bounty wins
}

// ============================================
// Match Data Serialization Flow
// ============================================

inline void matchSerializationRoundTrip() {
    // Simulate creating a match on device A
    Match originalMatch(
        TestConstants::TEST_UUID_MATCH_1,
        TestConstants::TEST_UUID_NPC_1,
        TestConstants::TEST_UUID_NPC_2
    );
    originalMatch.setHunterDrawTime(225);
    originalMatch.setBountyDrawTime(310);

    // Serialize to binary (as if sending over ESP-NOW)
    uint8_t buffer[MATCH_BINARY_SIZE];
    size_t bytesWritten = originalMatch.serialize(buffer);
    EXPECT_EQ(bytesWritten, MATCH_BINARY_SIZE);

    // Simulate receiving on device B and deserializing
    Match receivedMatch;
    size_t bytesRead = receivedMatch.deserialize(buffer);
    EXPECT_EQ(bytesRead, MATCH_BINARY_SIZE);

    // Verify data integrity
    EXPECT_EQ(receivedMatch.getMatchId(), originalMatch.getMatchId());
    EXPECT_EQ(receivedMatch.getHunterId(), originalMatch.getHunterId());
    EXPECT_EQ(receivedMatch.getBountyId(), originalMatch.getBountyId());
    EXPECT_EQ(receivedMatch.getHunterDrawTime(), 225);
    EXPECT_EQ(receivedMatch.getBountyDrawTime(), 310);

    // Also verify JSON serialization
    std::string json = receivedMatch.toJson();
    Match jsonRestored;
    jsonRestored.fromJson(json);

    EXPECT_EQ(jsonRestored.getMatchId(), originalMatch.getMatchId());
    EXPECT_EQ(jsonRestored.getHunterDrawTime(), 225);
}

// ============================================
// Player Stats Flow Over Multiple Matches
// ============================================

inline void playerStatsAccumulateAcrossMatches(Player* player) {
    // Simulate playing 5 matches: W, W, W, L, W
    
    // Match 1: Win
    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(200);

    // Match 2: Win
    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(180);

    // Match 3: Win
    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(220);

    EXPECT_EQ(player->getStreak(), 3);
    EXPECT_EQ(player->getWins(), 3);

    // Match 4: Loss
    player->incrementLosses();
    player->incrementMatchesPlayed();
    player->resetStreak();
    player->addReactionTime(350); // Slow reaction led to loss

    EXPECT_EQ(player->getStreak(), 0); // Streak reset
    EXPECT_EQ(player->getLosses(), 1);

    // Match 5: Win
    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(190);

    // Final stats check
    EXPECT_EQ(player->getWins(), 4);
    EXPECT_EQ(player->getLosses(), 1);
    EXPECT_EQ(player->getMatchesPlayed(), 5);
    EXPECT_EQ(player->getStreak(), 1);
    EXPECT_EQ(player->getLastReactionTime(), 190);
    
    // Average: (200+180+220+350+190) / 5 = 228
    EXPECT_EQ(player->getAverageReactionTime(), 228);
}

// ============================================
// Edge Cases
// ============================================

inline void duelWithTiedReactionTimes(DuelIntegrationTestSuite* suite) {
    suite->initializeAsHunterDevice();

    Match* match = suite->matchManager->createMatch(
        TestConstants::TEST_MATCH_ID_TIE,
        suite->hunter->getUserID(),
        TestConstants::TEST_UUID_BOUNTY_1
    );
    match->setHunterDrawTime(250);
    match->setBountyDrawTime(250); // Exact tie
    
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    // With equal times, hunter_time < bounty_time is false, so hunter loses
    EXPECT_FALSE(suite->matchManager->didWin());
}

inline void duelWithOpponentTimeout(DuelIntegrationTestSuite* suite) {
    suite->initializeAsHunterDevice();

    Match* match = suite->matchManager->createMatch(
        TestConstants::TEST_MATCH_ID_TIMEOUT,
        suite->hunter->getUserID(),
        TestConstants::TEST_UUID_BOUNTY_2
    );
    match->setHunterDrawTime(300);
    match->setBountyDrawTime(0); // Opponent never pressed
    
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setNeverPressed(); // Grace period expired

    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin()); // Win by opponent timeout
}
