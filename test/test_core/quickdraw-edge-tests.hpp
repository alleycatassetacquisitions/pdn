#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "utility-tests.hpp"
#include "device/device-constants.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::DoAll;

// ============================================
// Quickdraw Edge Case Tests
// Tests for boundary conditions, simultaneous events, and edge cases
// ============================================

class QuickdrawEdgeCaseTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        player = new Player();
        char playerId[] = "1234";
        player->setUserID(playerId);
        player->setIsHunter(true);
        player->setOpponentMacAddress("AA:BB:CC:DD:EE:FF");

        matchManager = new MatchManager();
        wirelessManager = new MockQuickdrawWirelessManager();
        wirelessManager->initialize(player, device.wirelessManager, 100);
        matchManager->initialize(player, &storage, &peerComms, wirelessManager);

        matchManager->createMatch("test-match-id", player->getUserID(), "5678");

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    MockQuickdrawWirelessManager* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    FakePlatformClock* fakeClock;
};

// ============================================
// Timeout Edge Cases
// ============================================

// Test: Timeout at exactly 4000ms boundary condition
inline void duelTimeoutAtExactBoundary(QuickdrawEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    // Advance to exactly 3999ms - should not timeout yet
    suite->fakeClock->advance(3999);
    duelState.onStateLoop(&suite->device);
    EXPECT_FALSE(duelState.transitionToIdle());

    // Advance to 4000ms - should timeout (timer uses <=)
    suite->fakeClock->advance(1);
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToIdle());
}

// Test: Very short timeout (1ms)
inline void duelVeryShortTimeout(QuickdrawEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    // Advance by 1ms
    suite->fakeClock->advance(1);
    duelState.onStateLoop(&suite->device);

    // Should not timeout (4000ms timeout configured)
    EXPECT_FALSE(duelState.transitionToIdle());
}

// Test: Both players timeout simultaneously
inline void duelBothPlayersTimeout(QuickdrawEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    // Neither player presses button
    // Advance past timeout
    suite->fakeClock->advance(4100);
    duelState.onStateLoop(&suite->device);

    // Should transition to idle (both timed out)
    EXPECT_TRUE(duelState.transitionToIdle());
}

// Test: Grace period expires at exact boundary (DuelPushed)
inline void duelPushedGracePeriodExactBoundary(QuickdrawEdgeCaseTests* suite) {
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);

    DuelPushed pushedState(suite->player, suite->matchManager);
    pushedState.onStateMounted(&suite->device);

    // Advance to 899ms - should not transition yet
    suite->fakeClock->advance(899);
    pushedState.onStateLoop(&suite->device);
    EXPECT_FALSE(pushedState.transitionToDuelResult());

    // Advance to 900ms - should transition (timer uses <=)
    suite->fakeClock->advance(1);
    pushedState.onStateLoop(&suite->device);
    EXPECT_TRUE(pushedState.transitionToDuelResult());
}

// Test: Grace period expires at exact boundary (DuelReceivedResult)
inline void duelReceivedResultGracePeriodExactBoundary(QuickdrawEdgeCaseTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    DuelReceivedResult receivedState(suite->player, suite->matchManager, suite->wirelessManager);
    receivedState.onStateMounted(&suite->device);

    // Advance to 749ms - should not transition yet
    suite->fakeClock->advance(749);
    receivedState.onStateLoop(&suite->device);
    EXPECT_FALSE(receivedState.transitionToDuelResult());

    // Advance to 750ms - should transition (timer uses <=)
    suite->fakeClock->advance(1);
    receivedState.onStateLoop(&suite->device);
    EXPECT_TRUE(receivedState.transitionToDuelResult());
}

// ============================================
// Simultaneous Press Edge Cases
// ============================================

// Test: Both players press at exactly the same time (exact tie)
inline void duelExactSimultaneousPress(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);

    // Both press at exactly 250ms
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setBountyDrawTime(250);

    // Tied times favor opponent
    EXPECT_FALSE(suite->matchManager->didWin());
}

// Test: Players press within 1ms of each other
inline void duelPressWithin1ms(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);

    // Hunter presses at 250ms, bounty at 251ms
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setBountyDrawTime(251);

    // Hunter should win (faster by 1ms)
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Both players false start before draw signal
inline void duelBothFalseStartBeforeSignal(QuickdrawEdgeCaseTests* suite) {
    // Both players press during countdown
    // This would result in button masher penalties for both
    DuelCountdown countdownState(suite->player, suite->matchManager);

    parameterizedCallbackFunction masherCallback = nullptr;
    void* masherCtx = nullptr;
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillOnce(DoAll(SaveArg<0>(&masherCallback), SaveArg<1>(&masherCtx)));
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    countdownState.onStateMounted(&suite->device);

    // Simulate false start press
    ASSERT_NE(masherCallback, nullptr);
    masherCallback(masherCtx);

    // Button masher count should be incremented
    // (Penalty is applied during the actual duel button press - tested elsewhere)
    SUCCEED();
}

// ============================================
// Result Calculation Edge Cases
// ============================================

// Test: Zero reaction time (impossible but edge case)
inline void duelZeroReactionTime(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);

    // Hunter presses at 0ms (instant)
    suite->matchManager->setHunterDrawTime(0);
    suite->matchManager->setBountyDrawTime(200);

    // Should still evaluate correctly - hunter wins
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Maximum reasonable reaction time
inline void duelMaximumReactionTime(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);

    // Hunter presses at 3999ms (just before timeout)
    suite->matchManager->setHunterDrawTime(3999);
    suite->matchManager->setBountyDrawTime(200);

    // Hunter should lose (much slower)
    EXPECT_FALSE(suite->matchManager->didWin());
}

// Test: Opponent times out with NEVER_PRESSED sentinel value
inline void duelOpponentNeverPressedSentinel(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);

    // Hunter presses at 500ms
    suite->matchManager->setHunterDrawTime(500);

    // Bounty never presses (timeout)
    suite->matchManager->setNeverPressed();

    // Hunter should win
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Button masher penalty accumulation (multiple presses)
inline void duelMaxButtonMasherPenalty(QuickdrawEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    DuelCountdown countdownState(suite->player, suite->matchManager);

    parameterizedCallbackFunction masherCallback = nullptr;
    void* masherCtx = nullptr;
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .WillOnce(DoAll(SaveArg<0>(&masherCallback), SaveArg<1>(&masherCtx)));
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    countdownState.onStateMounted(&suite->device);

    // Simulate 5 early presses
    ASSERT_NE(masherCallback, nullptr);
    for (int i = 0; i < 5; i++) {
        masherCallback(masherCtx);
    }

    // Clean up countdown
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    countdownState.onStateDismounted(&suite->device);

    // Now start the duel and press button to see accumulated penalty
    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    duelState.onStateMounted(&suite->device);

    // Advance 200ms
    suite->fakeClock->advance(200);

    // Trigger button press
    suite->matchManager->getDuelButtonPush()(suite->matchManager);

    // Reaction time should be 200ms + (5 * 75ms penalty) = 575ms
    Match* match = suite->matchManager->getCurrentMatch();
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->getHunterDrawTime(), 575);
}

// ============================================
// State Transition Edge Cases
// ============================================

// Test: Receive result at exact moment of button press
inline void duelReceiveResultAtButtonPress(QuickdrawEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    suite->fakeClock->advance(200);

    // Simulate both receiving result and pressing button in same instant
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    suite->matchManager->getDuelButtonPush()(suite->matchManager);

    duelState.onStateLoop(&suite->device);

    // Should prioritize received result (checked first in onStateLoop)
    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
    EXPECT_FALSE(duelState.transitionToDuelPushed());
}

// Test: Multiple state loops without time advancement
inline void duelMultipleLoopsWithoutTimeAdvancement(QuickdrawEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    // Call loop multiple times without advancing clock
    for (int i = 0; i < 10; i++) {
        duelState.onStateLoop(&suite->device);
    }

    // Should not timeout
    EXPECT_FALSE(duelState.transitionToIdle());
}

// Test: Dismount before timeout completes
inline void duelDismountBeforeTimeout(QuickdrawEdgeCaseTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, suite->wirelessManager);
    duelState.onStateMounted(&suite->device);

    // Advance partway through timeout
    suite->fakeClock->advance(2000);

    // Dismount before timeout completes
    duelState.onStateDismounted(&suite->device);

    // Transition flags should be reset
    EXPECT_FALSE(duelState.transitionToIdle());
    EXPECT_FALSE(duelState.transitionToDuelPushed());
    EXPECT_FALSE(duelState.transitionToDuelReceivedResult());
}

// ============================================
// Result Propagation Edge Cases
// ============================================

// Test: Win result updates player stats correctly
inline void resultWinUpdatesPlayerStats(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);
    int initialWins = suite->player->getWins();
    int initialLosses = suite->player->getLosses();

    // Hunter wins
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);

    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);

    // Should transition to win
    EXPECT_TRUE(resultState.transitionToWin());

    // Stats should be updated
    EXPECT_EQ(suite->player->getWins(), initialWins + 1);
    EXPECT_EQ(suite->player->getLosses(), initialLosses);
}

// Test: Lose result updates player stats correctly
inline void resultLoseUpdatesPlayerStats(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);
    int initialWins = suite->player->getWins();
    int initialLosses = suite->player->getLosses();

    // Hunter loses
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setBountyDrawTime(200);

    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);

    // Should transition to lose
    EXPECT_TRUE(resultState.transitionToLose());

    // Stats should be updated
    EXPECT_EQ(suite->player->getWins(), initialWins);
    EXPECT_EQ(suite->player->getLosses(), initialLosses + 1);
}

// Test: Win streak increments on consecutive wins
inline void resultWinStreakIncrementsOnWin(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);
    int initialWinStreak = suite->player->getStreak();

    // Hunter wins
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);

    DuelResult resultState(suite->player, suite->matchManager, suite->wirelessManager);
    resultState.onStateMounted(&suite->device);

    // Win streak should increment
    EXPECT_EQ(suite->player->getStreak(), initialWinStreak + 1);
}

// Test: Win streak resets on loss
inline void resultWinStreakResetsOnLoss(QuickdrawEdgeCaseTests* suite) {
    suite->player->setIsHunter(true);

    // Set up a win streak by simulating two wins
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    DuelResult win1State(suite->player, suite->matchManager, suite->wirelessManager);
    win1State.onStateMounted(&suite->device);

    suite->matchManager->clearCurrentMatch();
    suite->matchManager->createMatch("test-match-id-2", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    DuelResult win2State(suite->player, suite->matchManager, suite->wirelessManager);
    win2State.onStateMounted(&suite->device);

    int streakAfterWins = suite->player->getStreak();
    EXPECT_GT(streakAfterWins, 0); // Should have a streak

    // Now lose - hunter loses
    suite->matchManager->clearCurrentMatch();
    suite->matchManager->createMatch("test-match-id-3", suite->player->getUserID(), "5678");
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setBountyDrawTime(200);

    DuelResult loseState(suite->player, suite->matchManager, suite->wirelessManager);
    loseState.onStateMounted(&suite->device);

    // Win streak should reset to 0
    EXPECT_EQ(suite->player->getStreak(), 0);
}
