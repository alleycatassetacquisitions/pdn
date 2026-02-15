#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "../test-constants.hpp"

// ============================================
// MatchManager Test Fixture
// ============================================

class MatchManagerTestSuite : public testing::Test {
protected:
    void SetUp() override {
        // Create MatchManager instance
        matchManager = new MatchManager();
        
        // Create a test player
        player = new Player();
        player->setUserID(const_cast<char*>(TestConstants::TEST_ID_PLAYER));
        
        // Initialize match manager with mocks (4 parameters now)
        matchManager->initialize(player, &mockStorage, &mockPeerComms, &mockWirelessManager);
        
        // Clear any existing match
        matchManager->clearCurrentMatch();
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete player;
    }

    MatchManager* matchManager;
    Player* player;
    MockStorage mockStorage;
    MockPeerComms mockPeerComms;
    MockQuickdrawWirelessManager mockWirelessManager;
};

// ============================================
// Match Creation Tests
// ============================================

inline void matchManagerCreatesMatchCorrectly(MatchManager* mm, Player* player) {
    Match* match = mm->createMatch("match-123", "hunter-uuid", "bounty-uuid");

    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->getMatchId(), "match-123");
    EXPECT_EQ(match->getHunterId(), "hunter-uuid");
    EXPECT_EQ(match->getBountyId(), "bounty-uuid");
    EXPECT_EQ(match->getHunterDrawTime(), 0);
    EXPECT_EQ(match->getBountyDrawTime(), 0);
}

inline void matchManagerPreventsMultipleActiveMatches(MatchManager* mm) {
    Match* first = mm->createMatch(TestConstants::TEST_MATCH_ID_1, TestConstants::TEST_UUID_HUNTER_1, TestConstants::TEST_UUID_BOUNTY_1);
    ASSERT_NE(first, nullptr);

    // Second create should fail
    Match* second = mm->createMatch(TestConstants::TEST_MATCH_ID_2, TestConstants::TEST_UUID_HUNTER_2, TestConstants::TEST_UUID_BOUNTY_2);
    EXPECT_EQ(second, nullptr);

    // Current match should still be the first one
    EXPECT_EQ(mm->getCurrentMatch()->getMatchId(), TestConstants::TEST_MATCH_ID_1);
}

inline void matchManagerReceiveMatchWorks(MatchManager* mm) {
    Match incoming("received-match", "hunter-x", "bounty-y");
    incoming.setHunterDrawTime(100);
    incoming.setBountyDrawTime(200);

    Match* received = mm->receiveMatch(incoming);

    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->getMatchId(), "received-match");
    EXPECT_EQ(received->getHunterDrawTime(), 100);
    EXPECT_EQ(received->getBountyDrawTime(), 200);
}

// ============================================
// Win Determination Tests
// ============================================

inline void matchManagerHunterWinsWhenFaster(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    
    Match* match = mm->createMatch("duel-1", player->getUserID(), "bounty-opponent");
    match->setHunterDrawTime(200);
    match->setBountyDrawTime(300);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerHunterLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    
    Match* match = mm->createMatch("duel-2", player->getUserID(), "bounty-opponent");
    match->setHunterDrawTime(350);
    match->setBountyDrawTime(200);

    EXPECT_FALSE(mm->didWin());
}

inline void matchManagerBountyWinsWhenFaster(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    
    Match* match = mm->createMatch("duel-3", "hunter-opponent", player->getUserID());
    match->setHunterDrawTime(400);
    match->setBountyDrawTime(250);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    
    Match* match = mm->createMatch("duel-4", "hunter-opponent", player->getUserID());
    match->setHunterDrawTime(150);
    match->setBountyDrawTime(300);

    EXPECT_FALSE(mm->didWin());
}

// Special case: opponent never pressed (time = 0)
inline void matchManagerHunterWinsWhenBountyNeverPressed(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    
    Match* match = mm->createMatch("duel-5", player->getUserID(), "bounty-afk");
    match->setHunterDrawTime(250);
    match->setBountyDrawTime(0); // Bounty never pressed

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyWinsWhenHunterNeverPressed(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    
    Match* match = mm->createMatch("duel-6", "hunter-afk", player->getUserID());
    match->setHunterDrawTime(0); // Hunter never pressed
    match->setBountyDrawTime(300);

    EXPECT_TRUE(mm->didWin());
}

// ============================================
// Match State Tests
// ============================================

inline void matchManagerTracksDuelState(MatchManager* mm) {
    mm->createMatch("state-test", "hunter", "bounty");

    EXPECT_FALSE(mm->getHasReceivedDrawResult());
    EXPECT_FALSE(mm->getHasPressedButton());
    EXPECT_FALSE(mm->matchResultsAreIn());

    mm->setReceivedButtonPush();
    EXPECT_TRUE(mm->getHasPressedButton());
    EXPECT_FALSE(mm->matchResultsAreIn()); // Still need draw result

    mm->setReceivedDrawResult();
    EXPECT_TRUE(mm->getHasReceivedDrawResult());
    EXPECT_TRUE(mm->matchResultsAreIn()); // Now complete
}

inline void matchManagerGracePeriodPath(MatchManager* mm) {
    mm->createMatch("grace-test", "hunter", "bounty");

    mm->setReceivedButtonPush();
    mm->setNeverPressed(); // Grace period expired with no opponent result

    EXPECT_TRUE(mm->matchResultsAreIn()); // Can proceed via grace period path
}

inline void matchManagerClearMatchResetsState(MatchManager* mm) {
    mm->createMatch("clear-test", "hunter", "bounty");
    mm->setReceivedButtonPush();
    mm->setReceivedDrawResult();
    mm->setDuelLocalStartTime(1000);

    mm->clearCurrentMatch();

    EXPECT_EQ(mm->getCurrentMatch(), nullptr);
    EXPECT_FALSE(mm->getHasReceivedDrawResult());
    EXPECT_FALSE(mm->getHasPressedButton());
}

// ============================================
// Draw Time Setting Tests
// ============================================

inline void matchManagerSetDrawTimesRequiresActiveMatch(MatchManager* mm) {
    // No active match
    EXPECT_FALSE(mm->setHunterDrawTime(100));
    EXPECT_FALSE(mm->setBountyDrawTime(200));

    // With active match
    mm->createMatch("time-test", "hunter", "bounty");
    EXPECT_TRUE(mm->setHunterDrawTime(100));
    EXPECT_TRUE(mm->setBountyDrawTime(200));

    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 100);
    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 200);
}

inline void matchManagerDuelStartTimeTracking(MatchManager* mm) {
    mm->createMatch("start-time-test", "hunter", "bounty");

    EXPECT_EQ(mm->getDuelLocalStartTime(), 0);

    mm->setDuelLocalStartTime(5000);
    EXPECT_EQ(mm->getDuelLocalStartTime(), 5000);
}
