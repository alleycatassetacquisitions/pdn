#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// GHOST RUNNER TEST SUITE
// ============================================

class GhostRunnerTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createGameDevice(0, "ghost-runner");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = static_cast<GhostRunner*>(device_.game);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            device_.clockDriver->advance(delayMs);
            device_.pdn->loop();
        }
    }

    DeviceInstance device_;
    GhostRunner* game_ = nullptr;
};

// ============================================
// GHOST RUNNER MANAGED MODE TEST SUITE
// ============================================

class GhostRunnerManagedTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            player_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            player_.pdn->loop();
        }
    }

    void advanceToIdle() {
        player_.game->skipToState(player_.pdn, 7);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentState()->getStateId();
    }

    GhostRunner* getGhostRunner() {
        return static_cast<GhostRunner*>(
            player_.pdn->getApp(StateId(GHOST_RUNNER_APP_ID)));
    }

    DeviceInstance player_;
};

// ============================================
// CONFIG PRESET TESTS
// ============================================

/*
 * Test: Easy config has wide target zone (30 units), 4 rounds, 3 misses.
 */
void ghostRunnerEasyConfigPresets(GhostRunnerTestSuite* suite) {
    GhostRunnerConfig easy = GHOST_RUNNER_EASY;
    ASSERT_EQ(easy.ghostSpeedMs, 50);
    ASSERT_EQ(easy.screenWidth, 100);
    ASSERT_EQ(easy.targetZoneStart, 35);
    ASSERT_EQ(easy.targetZoneEnd, 65);
    int zoneWidth = easy.targetZoneEnd - easy.targetZoneStart;
    ASSERT_EQ(zoneWidth, 30);
    ASSERT_EQ(easy.rounds, 4);
    ASSERT_EQ(easy.missesAllowed, 3);
}

/*
 * Test: Hard config has narrow target zone (16 units), 6 rounds, 1 miss.
 */
void ghostRunnerHardConfigPresets(GhostRunnerTestSuite* suite) {
    GhostRunnerConfig hard = GHOST_RUNNER_HARD;
    ASSERT_EQ(hard.ghostSpeedMs, 30);
    ASSERT_EQ(hard.screenWidth, 100);
    ASSERT_EQ(hard.targetZoneStart, 42);
    ASSERT_EQ(hard.targetZoneEnd, 58);
    int zoneWidth = hard.targetZoneEnd - hard.targetZoneStart;
    ASSERT_EQ(zoneWidth, 16);
    ASSERT_EQ(hard.rounds, 6);
    ASSERT_EQ(hard.missesAllowed, 1);
}

// ============================================
// INTRO STATE TESTS
// ============================================

/*
 * Test: Intro resets session on mount (score, strikes, round all zero).
 */
void ghostRunnerIntroSeedsRng(GhostRunnerTestSuite* suite) {
    // Dirty the session before entering intro
    suite->game_->getSession().score = 999;
    suite->game_->getSession().strikes = 5;
    suite->game_->getSession().currentRound = 10;

    // Re-enter intro (skipToState 0 = Intro)
    suite->game_->skipToState(suite->device_.pdn, 0);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.score, 0);
    ASSERT_EQ(session.strikes, 0);
    ASSERT_EQ(session.currentRound, 0);
    ASSERT_EQ(session.ghostPosition, 0);
    ASSERT_FALSE(session.playerPressed);
}

/*
 * Test: Intro transitions to Show after timer expires.
 * Note: Since the intro timer is set before setPlatformClock in the test
 * fixture, the timer expires immediately on first loop. We just need a
 * few ticks to process the transition.
 */
void ghostRunnerIntroTransitionsToShow(GhostRunnerTestSuite* suite) {
    State* state = suite->game_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), GHOST_INTRO);

    // Advance a few ticks — intro timer set before clock, expires immediately
    suite->tickWithTime(3, 100);

    state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_SHOW);
}

// ============================================
// SHOW STATE TESTS
// ============================================

/*
 * Test: Show state is entered and displays round info.
 */
void ghostRunnerShowDisplaysRoundInfo(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);  // index 1 = Show

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_SHOW);
}

/*
 * Test: Show transitions to Gameplay after timer.
 */
void ghostRunnerShowTransitionsToGameplay(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);  // index 1 = Show

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), GHOST_SHOW);

    // Advance past 1.5s show timer
    suite->tickWithTime(20, 100);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_GAMEPLAY);
}

// ============================================
// GAMEPLAY STATE TESTS
// ============================================

/*
 * Test: Ghost position advances with time in gameplay state.
 */
void ghostRunnerGhostAdvancesWithTime(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 10;
    suite->game_->skipToState(suite->device_.pdn, 2);  // index 2 = Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.ghostPosition, 0);

    // Advance 5 steps worth of time (5 * 10ms = 50ms, plus some extra)
    suite->tickWithTime(5, 15);

    ASSERT_GT(session.ghostPosition, 0);
}

/*
 * Test: Correct press in target zone — player pressed in zone, score increases.
 */
void ghostRunnerCorrectPressInTargetZone(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 10;
    suite->game_->getConfig().targetZoneStart = 5;
    suite->game_->getConfig().targetZoneEnd = 95;
    suite->game_->getConfig().rounds = 4;
    suite->game_->getConfig().missesAllowed = 3;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Advance ghost into target zone
    suite->tickWithTime(10, 15);

    auto& session = suite->game_->getSession();
    ASSERT_GE(session.ghostPosition, 5) << "Ghost should be in target zone";
    ASSERT_FALSE(session.playerPressed);

    // Press button while in zone
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    // Should have transitioned to Evaluate, which routes to Show for next round
    State* state = suite->game_->getCurrentState();
    ASSERT_TRUE(state->getStateId() == GHOST_EVALUATE ||
                state->getStateId() == GHOST_SHOW) << "Should advance after hit";

    ASSERT_EQ(session.score, 100) << "Score should increase on hit";
    ASSERT_EQ(session.strikes, 0) << "Strikes should not increase on hit";
}

/*
 * Test: Incorrect press outside target zone — strike.
 */
void ghostRunnerIncorrectPressOutsideZone(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 50;
    suite->game_->getConfig().targetZoneStart = 80;
    suite->game_->getConfig().targetZoneEnd = 90;
    suite->game_->getConfig().rounds = 4;
    suite->game_->getConfig().missesAllowed = 3;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Ghost is at position 0, target zone is 80-90. Press immediately = miss
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.score, 0) << "Score should not increase on miss";
    ASSERT_EQ(session.strikes, 1) << "Strike should be counted on miss";
}

/*
 * Test: Ghost timeout counts as strike (ghost reaches screenWidth without press).
 */
void ghostRunnerGhostTimeoutCountsStrike(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 5;
    suite->game_->getConfig().screenWidth = 10;  // Short screen for quick timeout
    suite->game_->getConfig().targetZoneStart = 3;
    suite->game_->getConfig().targetZoneEnd = 7;
    suite->game_->getConfig().rounds = 4;
    suite->game_->getConfig().missesAllowed = 3;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Advance enough for ghost to reach end (10 positions * 5ms = 50ms)
    suite->tickWithTime(20, 10);

    auto& session = suite->game_->getSession();
    // Ghost should have reached end, triggered evaluate, counted a strike
    ASSERT_EQ(session.strikes, 1) << "Timeout should count as strike";
    ASSERT_FALSE(session.playerPressed) << "Player did not press";
}

// ============================================
// EVALUATE STATE TESTS
// ============================================

/*
 * Test: Evaluate routes to next round (Show) after a hit.
 */
void ghostRunnerEvaluateRoutesToNextRound(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.currentRound = 0;
    session.ghostPosition = 50;  // In default target zone (40-60)
    session.playerPressed = true;
    session.strikes = 0;
    suite->game_->getConfig().rounds = 4;
    suite->game_->getConfig().missesAllowed = 3;

    suite->game_->skipToState(suite->device_.pdn, 3);  // index 3 = Evaluate
    suite->tick(3);

    ASSERT_EQ(session.currentRound, 1) << "Round should advance";
    ASSERT_EQ(session.score, 100) << "Score should increase on hit";

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_SHOW) << "Should go to Show for next round";
}

/*
 * Test: Evaluate routes to Win when all rounds completed.
 */
void ghostRunnerEvaluateRoutesToWin(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.currentRound = 3;   // Last round (0-indexed, rounds=4)
    session.ghostPosition = 50; // In target zone
    session.playerPressed = true;
    session.strikes = 0;
    suite->game_->getConfig().rounds = 4;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_WIN);
}

/*
 * Test: Evaluate routes to Lose when too many strikes.
 */
void ghostRunnerEvaluateRoutesToLose(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.currentRound = 0;
    session.ghostPosition = 0;   // Outside target zone
    session.playerPressed = true; // Pressed outside zone = strike
    session.strikes = 2;         // Already 2 strikes
    suite->game_->getConfig().missesAllowed = 2;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    // 2 existing + 1 new = 3 > 2 allowed -> lose
    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_LOSE);
}

// ============================================
// OUTCOME TESTS
// ============================================

/*
 * Test: Win state sets outcome to WON with score and correct hardMode.
 */
void ghostRunnerWinSetsOutcome(GhostRunnerTestSuite* suite) {
    suite->game_->getSession().score = 300;
    suite->game_->skipToState(suite->device_.pdn, 4);  // index 4 = Win

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::WON);
    ASSERT_EQ(outcome.score, 300);
    // Easy config: missesAllowed=3, zoneWidth=30 -> hardMode=false
    ASSERT_FALSE(outcome.hardMode);
}

/*
 * Test: Lose state sets outcome to LOST.
 */
void ghostRunnerLoseSetsOutcome(GhostRunnerTestSuite* suite) {
    suite->game_->getSession().score = 100;
    suite->game_->skipToState(suite->device_.pdn, 5);  // index 5 = Lose

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::LOST);
    ASSERT_EQ(outcome.score, 100);
}

// ============================================
// MODE TESTS
// ============================================

/*
 * Test: In standalone mode, Win loops back to Intro.
 */
void ghostRunnerStandaloneLoopsToIntro(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().managedMode = false;
    suite->game_->skipToState(suite->device_.pdn, 4);  // Win

    // Advance past 3s win display timer
    suite->tickWithTime(40, 100);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_INTRO);
}

/*
 * Test: In managed mode (FDN), Win returns to FdnComplete.
 */
void ghostRunnerManagedModeReturns(GhostRunnerManagedTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN handshake for Ghost Runner (GameType 1, KonamiButton UP=0)
    suite->player_.serialOutDriver->injectInput("*fdn:1:0\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Should be in Ghost Runner now
    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);
    ASSERT_TRUE(gr->getConfig().managedMode);

    // Advance past intro timer (2s)
    suite->tickWithTime(25, 100);

    // Should be in Show state
    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_SHOW);

    // Advance past show timer (1.5s)
    suite->tickWithTime(20, 100);

    // Should be in Gameplay
    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_GAMEPLAY);

    // Set up config for quick win — wide zone, short screen
    gr->getConfig().ghostSpeedMs = 5;
    gr->getConfig().screenWidth = 100;
    gr->getConfig().targetZoneStart = 5;
    gr->getConfig().targetZoneEnd = 95;
    gr->getConfig().rounds = 1;

    // Advance ghost into zone
    suite->tickWithTime(10, 10);

    // Press button in zone
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Should be in Win now
    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_WIN);
    ASSERT_EQ(gr->getOutcome().result, MiniGameResult::WON);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

// ============================================
// STATE NAME TESTS
// ============================================

/*
 * Test: All Ghost Runner state names resolve correctly.
 */
void ghostRunnerStateNamesResolve(GhostRunnerTestSuite* suite) {
    ASSERT_STREQ(getGhostRunnerStateName(GHOST_INTRO), "GhostRunnerIntro");
    ASSERT_STREQ(getGhostRunnerStateName(GHOST_WIN), "GhostRunnerWin");
    ASSERT_STREQ(getGhostRunnerStateName(GHOST_LOSE), "GhostRunnerLose");
    ASSERT_STREQ(getGhostRunnerStateName(GHOST_SHOW), "GhostRunnerShow");
    ASSERT_STREQ(getGhostRunnerStateName(GHOST_GAMEPLAY), "GhostRunnerGameplay");
    ASSERT_STREQ(getGhostRunnerStateName(GHOST_EVALUATE), "GhostRunnerEvaluate");

    // Verify getStateName routes correctly
    ASSERT_STREQ(getStateName(GHOST_INTRO), "GhostRunnerIntro");
    ASSERT_STREQ(getStateName(GHOST_GAMEPLAY), "GhostRunnerGameplay");
}

#endif // NATIVE_BUILD
