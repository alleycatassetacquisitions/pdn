#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
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
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createGameDevice(0, "ghost-runner");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = static_cast<GhostRunner*>(device_.game);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
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
        player_.game->skipToState(player_.pdn, 6);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentStateId();
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
 * Test: Easy config has wide hit zone, no dual-lane notes, 4 rounds, 3 lives.
 */
void ghostRunnerEasyConfigPresets(GhostRunnerTestSuite* suite) {
    GhostRunnerConfig easy = GHOST_RUNNER_EASY;
    ASSERT_EQ(easy.ghostSpeedMs, 50);
    ASSERT_EQ(easy.rounds, 4);
    ASSERT_EQ(easy.notesPerRound, 8);
    ASSERT_EQ(easy.dualLaneChance, 0.0f);  // no dual-lane notes in easy
    ASSERT_EQ(easy.holdNoteChance, 0.15f);
    ASSERT_EQ(easy.lives, 3);
    ASSERT_EQ(easy.hitZoneWidthPx, 20);  // wide hit zone
    ASSERT_EQ(easy.perfectZonePx, 6);
    ASSERT_EQ(easy.speedRampPerRound, 1.0f);  // no speed increase
}

/*
 * Test: Hard config has narrow hit zone, dual-lane notes, speed ramp, 3 lives.
 */
void ghostRunnerHardConfigPresets(GhostRunnerTestSuite* suite) {
    GhostRunnerConfig hard = GHOST_RUNNER_HARD;
    ASSERT_EQ(hard.ghostSpeedMs, 30);
    ASSERT_EQ(hard.rounds, 4);
    ASSERT_EQ(hard.notesPerRound, 12);  // more notes
    ASSERT_EQ(hard.dualLaneChance, 0.4f);  // 40% dual-lane
    ASSERT_EQ(hard.holdNoteChance, 0.35f);  // more hold notes
    ASSERT_EQ(hard.lives, 3);
    ASSERT_EQ(hard.hitZoneWidthPx, 14);  // narrow hit zone
    ASSERT_EQ(hard.perfectZonePx, 3);
    ASSERT_EQ(hard.speedRampPerRound, 1.1f);  // 10% speed increase per round
}

// ============================================
// INTRO STATE TESTS
// ============================================

/*
 * Test: Intro resets session on mount (score, stats, lives restored).
 */
void ghostRunnerIntroSeedsRng(GhostRunnerTestSuite* suite) {
    // Dirty the session before entering intro
    suite->game_->getSession().score = 999;
    suite->game_->getSession().currentRound = 10;
    suite->game_->getSession().livesRemaining = 0;
    suite->game_->getSession().perfectCount = 50;
    suite->game_->getSession().missCount = 20;

    // Re-enter intro (skipToState 0 = Intro)
    suite->game_->skipToState(suite->device_.pdn, 0);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.score, 0);
    ASSERT_EQ(session.currentRound, 0);
    ASSERT_EQ(session.livesRemaining, 3);  // restored to config.lives
    ASSERT_EQ(session.perfectCount, 0);
    ASSERT_EQ(session.goodCount, 0);
    ASSERT_EQ(session.missCount, 0);
    ASSERT_TRUE(session.currentPattern.empty());
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

    ASSERT_EQ(suite->game_->getCurrentStateId(), GHOST_SHOW);

    // Advance past 1.5s show timer
    suite->tickWithTime(20, 100);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_GAMEPLAY);
}

// ============================================
// GAMEPLAY STATE TESTS
// ============================================

/*
 * Test: Notes scroll left with time in gameplay state.
 */
void ghostRunnerGhostAdvancesWithTime(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 10;
    suite->game_->getConfig().notesPerRound = 3;
    suite->game_->getConfig().rngSeed = 42;  // deterministic
    suite->game_->skipToState(suite->device_.pdn, 2);  // index 2 = Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    ASSERT_FALSE(session.currentPattern.empty());

    // Record initial x position of first note
    int initialX = session.currentPattern[0].xPosition;
    ASSERT_EQ(initialX, 128);  // starts offscreen right

    // Advance 5 steps worth of time (5 * 10ms = 50ms)
    suite->tickWithTime(5, 15);

    // Note should have moved left
    ASSERT_LT(session.currentPattern[0].xPosition, initialX);
}

/*
 * Test: Correct press in hit zone — note graded GOOD or PERFECT, score increases.
 */
void ghostRunnerCorrectPressInTargetZone(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 10;
    suite->game_->getConfig().notesPerRound = 1;
    suite->game_->getConfig().rounds = 1;
    suite->game_->getConfig().rngSeed = 42;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.currentPattern.size(), 1);

    // Move note to hit zone (x=8, width=20 -> range 8-28)
    session.currentPattern[0].xPosition = 15;  // center of hit zone
    session.currentPattern[0].lane = Lane::UP;
    session.currentPattern[0].type = NoteType::PRESS;

    // Press PRIMARY button (UP lane)
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    // Note should be graded
    ASSERT_NE(session.currentPattern[0].grade, NoteGrade::NONE);
    ASSERT_NE(session.currentPattern[0].grade, NoteGrade::MISS);

    // Score should increase (50 for GOOD, 100 for PERFECT)
    ASSERT_GT(session.score, 0) << "Score should increase on hit";
    ASSERT_EQ(session.livesRemaining, 3) << "Lives should not decrease on hit";
}

/*
 * Test: Incorrect press outside hit zone — graded MISS, loses life.
 */
void ghostRunnerIncorrectPressOutsideZone(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 50;
    suite->game_->getConfig().notesPerRound = 1;
    suite->game_->getConfig().rounds = 1;
    suite->game_->getConfig().rngSeed = 42;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.currentPattern.size(), 1);

    // Move note far outside hit zone (hit zone: x=8, width=20 -> range 8-28)
    session.currentPattern[0].xPosition = 60;  // far right of hit zone
    session.currentPattern[0].lane = Lane::UP;
    session.currentPattern[0].type = NoteType::PRESS;

    // Press PRIMARY button while note is outside hit zone
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    // Note should be graded MISS
    ASSERT_EQ(session.currentPattern[0].grade, NoteGrade::MISS);
    ASSERT_EQ(session.score, 0) << "Score should not increase on miss";
    ASSERT_EQ(session.livesRemaining, 2) << "Should lose 1 life on miss";
    ASSERT_EQ(session.missCount, 1);
}

/*
 * Test: Note passing hit zone without press counts as miss, loses life.
 */
void ghostRunnerGhostTimeoutCountsStrike(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 5;
    suite->game_->getConfig().notesPerRound = 1;
    suite->game_->getConfig().rounds = 1;
    suite->game_->getConfig().rngSeed = 42;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.currentPattern.size(), 1);

    // Note starts at x=128, hit zone at x=8. Let note scroll past hit zone.
    // Advance enough for note to pass hit zone (128 - 8 = 120 pixels, 5ms/pixel = 600ms)
    suite->tickWithTime(150, 5);

    // Note should be marked as missed (passed hit zone without being hit)
    ASSERT_EQ(session.currentPattern[0].grade, NoteGrade::MISS);
    ASSERT_FALSE(session.currentPattern[0].active);
    ASSERT_EQ(session.missCount, 1) << "Timeout should count as miss";
    ASSERT_EQ(session.livesRemaining, 2) << "Should lose 1 life";
}

// ============================================
// EVALUATE STATE TESTS
// ============================================

/*
 * Test: Evaluate routes to next round (Show) when round completes with lives remaining.
 */
void ghostRunnerEvaluateRoutesToNextRound(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    session.currentRound = 0;
    session.livesRemaining = 3;  // still alive
    session.score = 100;
    config.rounds = 4;

    suite->game_->skipToState(suite->device_.pdn, 3);  // index 3 = Evaluate
    suite->tick(3);

    ASSERT_EQ(session.currentRound, 1) << "Round should advance";

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_SHOW) << "Should go to Show for next round";
}

/*
 * Test: Evaluate routes to Win when all rounds completed with passing score.
 */
void ghostRunnerEvaluateRoutesToWin(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    session.currentRound = 3;   // Last round (0-indexed, rounds=4)
    session.livesRemaining = 2;
    session.perfectCount = 20;
    session.goodCount = 10;
    session.missCount = 0;     // No misses = win in easy mode
    config.rounds = 4;
    config.dualLaneChance = 0.0f;  // EASY mode

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_WIN);
}

/*
 * Test: Evaluate routes to Lose when no lives remaining.
 */
void ghostRunnerEvaluateRoutesToLose(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    session.currentRound = 0;
    session.livesRemaining = 0;  // No lives left
    session.missCount = 5;
    config.rounds = 4;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

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
    suite->game_->getConfig().dualLaneChance = 0.0f;  // EASY mode
    suite->game_->skipToState(suite->device_.pdn, 4);  // index 4 = Win

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::WON);
    ASSERT_EQ(outcome.score, 300);
    // Easy config: dualLaneChance=0.0 -> hardMode=false
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

    // Trigger FDN handshake for Ghost Runner (GameType 1, KonamiButton START=6)
    suite->player_.serialOutDriver->injectInput("*fdn:1:6\r");
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

    // Configure for quick win
    gr->getConfig().ghostSpeedMs = 5;
    gr->getConfig().notesPerRound = 1;
    gr->getConfig().rounds = 1;
    gr->getConfig().rngSeed = 42;

    // Advance past intro timer (2s)
    suite->tickWithTime(25, 100);

    // Should be in Show state
    ASSERT_EQ(gr->getCurrentStateId(), GHOST_SHOW);

    // Advance past show timer (1.5s)
    suite->tickWithTime(20, 100);

    // Should be in Gameplay
    ASSERT_EQ(gr->getCurrentStateId(), GHOST_GAMEPLAY);

    // Move note into hit zone and press
    auto& session = gr->getSession();
    session.currentPattern[0].xPosition = 15;  // center of hit zone
    session.currentPattern[0].lane = Lane::UP;

    // Press button in hit zone
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Should be in Evaluate, then Win
    suite->tick(5);
    ASSERT_EQ(gr->getCurrentStateId(), GHOST_WIN);
    ASSERT_EQ(gr->getOutcome().result, MiniGameResult::WON);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

// ============================================
// EDGE CASE TESTS
// ============================================

/*
 * Test: Press at exact hit zone start boundary counts as hit.
 */
void ghostRunnerPressAtZoneStartBoundary(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().hitZoneWidthPx = 20;
    suite->game_->getConfig().notesPerRound = 1;
    suite->game_->getConfig().rngSeed = 42;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    int initialScore = session.score;

    // Position note at exact hit zone start (x=8)
    session.currentPattern[0].xPosition = 8;
    session.currentPattern[0].lane = Lane::UP;
    session.currentPattern[0].type = NoteType::PRESS;

    // Press PRIMARY button
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    EXPECT_NE(session.currentPattern[0].grade, NoteGrade::MISS) << "Press at zone start should not be miss";
    EXPECT_GT(session.score, initialScore) << "Score should increase";
    EXPECT_EQ(session.livesRemaining, 3) << "No life should be lost";
}

/*
 * Test: Press at exact hit zone end boundary counts as hit.
 */
void ghostRunnerPressAtZoneEndBoundary(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().hitZoneWidthPx = 20;
    suite->game_->getConfig().notesPerRound = 1;
    suite->game_->getConfig().rngSeed = 42;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    int initialScore = session.score;

    // Position note at exact hit zone end (x=8 + width=20 = 28)
    session.currentPattern[0].xPosition = 28;
    session.currentPattern[0].lane = Lane::UP;
    session.currentPattern[0].type = NoteType::PRESS;

    // Press PRIMARY button
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    EXPECT_NE(session.currentPattern[0].grade, NoteGrade::MISS) << "Press at zone end should not be miss";
    EXPECT_GT(session.score, initialScore) << "Score should increase";
    EXPECT_EQ(session.livesRemaining, 3) << "No life should be lost";
}

/*
 * Test: Having exactly 1 life remaining doesn't lose (only 0 causes loss).
 */
void ghostRunnerExactStrikesEqualAllowed(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().rounds = 5;

    auto& session = suite->game_->getSession();
    session.currentRound = 0;
    session.livesRemaining = 1;  // Exactly 1 life left

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    // 1 life remaining -> should continue to next round, not lose
    State* state = suite->game_->getCurrentState();
    EXPECT_NE(state->getStateId(), GHOST_LOSE) << "Should not lose with 1 life remaining";
    EXPECT_EQ(state->getStateId(), GHOST_SHOW) << "Should advance to next round";
    EXPECT_EQ(session.currentRound, 1);
}

/*
 * Test: Note timeout with exactly 1 life remaining reduces to 0 lives, triggers lose.
 */
void ghostRunnerTimeoutAtExactMissesAllowed(GhostRunnerTestSuite* suite) {
    suite->game_->getConfig().ghostSpeedMs = 5;
    suite->game_->getConfig().notesPerRound = 1;
    suite->game_->getConfig().rounds = 4;
    suite->game_->getConfig().rngSeed = 42;

    auto& session = suite->game_->getSession();
    session.livesRemaining = 1;  // Only 1 life left

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Let note pass hit zone without pressing (timeout = miss, loses 1 life -> 0 lives)
    suite->tickWithTime(150, 5);

    // After timeout, should have 0 lives and transition to evaluate then lose
    suite->tick(5);

    EXPECT_EQ(session.livesRemaining, 0);
    State* state = suite->game_->getCurrentState();
    EXPECT_TRUE(state->getStateId() == GHOST_EVALUATE || state->getStateId() == GHOST_LOSE)
        << "Should transition to evaluate/lose with 0 lives";
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
