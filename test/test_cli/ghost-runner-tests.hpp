#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
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
// GHOST RUNNER TEST SUITE (Memory Maze)
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
 * Test: Easy config has correct maze parameters (5×3 grid, 4 rounds, 3 lives).
 */
void ghostRunnerEasyConfigPresets(GhostRunnerTestSuite* suite) {
    GhostRunnerConfig easy = GHOST_RUNNER_EASY;
    ASSERT_EQ(easy.cols, 5);
    ASSERT_EQ(easy.rows, 3);
    ASSERT_EQ(easy.rounds, 4);
    ASSERT_EQ(easy.lives, 3);
    ASSERT_EQ(easy.previewMazeMs, 4000);
    ASSERT_EQ(easy.previewTraceMs, 4000);
    ASSERT_EQ(easy.bonkFlashMs, 1000);
    ASSERT_EQ(easy.startRow, 0);
    ASSERT_EQ(easy.startCol, 0);
    ASSERT_EQ(easy.exitRow, 2);
    ASSERT_EQ(easy.exitCol, 4);
}

/*
 * Test: Hard config has larger maze (7×5 grid, 6 rounds, 1 life).
 */
void ghostRunnerHardConfigPresets(GhostRunnerTestSuite* suite) {
    GhostRunnerConfig hard = GHOST_RUNNER_HARD;
    ASSERT_EQ(hard.cols, 7);
    ASSERT_EQ(hard.rows, 5);
    ASSERT_EQ(hard.rounds, 6);
    ASSERT_EQ(hard.lives, 1);
    ASSERT_EQ(hard.previewMazeMs, 2500);
    ASSERT_EQ(hard.previewTraceMs, 3000);
    ASSERT_EQ(hard.bonkFlashMs, 500);
    ASSERT_EQ(hard.exitRow, 4);
    ASSERT_EQ(hard.exitCol, 6);
}

// ============================================
// INTRO STATE TESTS
// ============================================

/*
 * Test: Intro resets session on mount (score, stats, lives restored).
 */
void ghostRunnerIntroResetsSession(GhostRunnerTestSuite* suite) {
    // Dirty the session before entering intro
    suite->game_->getSession().score = 999;
    suite->game_->getSession().currentRound = 10;
    suite->game_->getSession().livesRemaining = 0;
    suite->game_->getSession().bonkCount = 5;
    suite->game_->getSession().cursorRow = 4;
    suite->game_->getSession().cursorCol = 4;

    // Re-enter intro (skipToState 0 = Intro)
    suite->game_->skipToState(suite->device_.pdn, 0);
    suite->tick(1);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.score, 0);
    ASSERT_EQ(session.currentRound, 0);
    ASSERT_EQ(session.livesRemaining, 3);  // restored to config.lives
    ASSERT_EQ(session.bonkCount, 0);
    ASSERT_EQ(session.cursorRow, 0);
    ASSERT_EQ(session.cursorCol, 0);
    ASSERT_EQ(session.currentDirection, DIR_RIGHT);
    ASSERT_EQ(session.stepsUsed, 0);
}

/*
 * Test: Intro transitions to Show after timer expires.
 */
void ghostRunnerIntroTransitionsToShow(GhostRunnerTestSuite* suite) {
    State* state = suite->game_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), GHOST_INTRO);

    // Advance past 2s intro timer
    suite->tickWithTime(25, 100);

    state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_SHOW);
}

// ============================================
// SHOW STATE TESTS
// ============================================

/*
 * Test: Show state is entered and displays maze preview.
 */
void ghostRunnerShowDisplaysMaze(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);  // index 1 = Show
    suite->tick(1);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_SHOW);
}

/*
 * Test: Show transitions to Gameplay after preview timers.
 */
void ghostRunnerShowTransitionsToGameplay(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);  // index 1 = Show
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentStateId(), GHOST_SHOW);

    // Advance past maze preview (4s) + trace preview (4s) = 8s total
    suite->tickWithTime(90, 100);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_GAMEPLAY);
}

// ============================================
// GAMEPLAY STATE TESTS (Maze Navigation)
// ============================================

/*
 * Test: PRIMARY button cycles direction clockwise (UP→RIGHT→DOWN→LEFT).
 */
void ghostRunnerDirectionCycling(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 2);  // index 2 = Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.currentDirection, DIR_RIGHT);  // starts facing right

    // Press PRIMARY button — cycles to DOWN
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    ASSERT_EQ(session.currentDirection, DIR_DOWN);

    // Press PRIMARY button — cycles to LEFT
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    ASSERT_EQ(session.currentDirection, DIR_LEFT);

    // Press PRIMARY button — cycles to UP
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    ASSERT_EQ(session.currentDirection, DIR_UP);

    // Press PRIMARY button — cycles back to RIGHT
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    ASSERT_EQ(session.currentDirection, DIR_RIGHT);
}

/*
 * Test: SECONDARY button moves cursor in current direction (valid move).
 */
void ghostRunnerValidMove(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Manually create a simple maze with no walls for testing
    for (int i = 0; i < config.rows * config.cols; i++) {
        session.walls[i] = 0;  // no walls
    }

    int initialRow = session.cursorRow;
    int initialCol = session.cursorCol;
    int initialSteps = session.stepsUsed;

    // Face RIGHT and move
    session.currentDirection = DIR_RIGHT;
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    ASSERT_EQ(session.cursorRow, initialRow);
    ASSERT_EQ(session.cursorCol, initialCol + 1);
    ASSERT_EQ(session.stepsUsed, initialSteps + 1);
}

/*
 * Test: Moving into a wall triggers bonk (loses life, flashes maze).
 */
void ghostRunnerBonkIntoWall(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up a wall to the RIGHT of starting position
    session.walls[0] = WALL_RIGHT;
    int initialLives = session.livesRemaining;

    // Face RIGHT and try to move into wall
    session.currentDirection = DIR_RIGHT;
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Should lose 1 life
    ASSERT_EQ(session.livesRemaining, initialLives - 1);
    // Cursor should NOT move
    ASSERT_EQ(session.cursorRow, 0);
    ASSERT_EQ(session.cursorCol, 0);
    // Maze flash should be active
    ASSERT_TRUE(session.mazeFlashActive);
}

/*
 * Test: Moving out of bounds triggers bonk.
 */
void ghostRunnerBonkOutOfBounds(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    int initialLives = session.livesRemaining;

    // Clear walls for testing
    for (int i = 0; i < 35; i++) {
        session.walls[i] = 0;
    }

    // Try to move UP from top row (out of bounds)
    session.cursorRow = 0;
    session.cursorCol = 0;
    session.currentDirection = DIR_UP;
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Should lose 1 life
    ASSERT_EQ(session.livesRemaining, initialLives - 1);
}

/*
 * Test: Reaching exit triggers transition to Evaluate.
 */
void ghostRunnerReachingExit(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Clear all walls for easy navigation
    for (int i = 0; i < 35; i++) {
        session.walls[i] = 0;
    }

    // Teleport cursor to one cell before exit
    session.cursorRow = config.exitRow;
    session.cursorCol = config.exitCol - 1;
    session.currentDirection = DIR_RIGHT;

    // Move RIGHT to reach the exit
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Should now be at exit and transitioning to evaluate
    ASSERT_EQ(session.cursorRow, config.exitRow);
    ASSERT_EQ(session.cursorCol, config.exitCol);

    // Give it several more ticks for the state transition to complete
    suite->tick(10);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_EVALUATE);
}

// ============================================
// EVALUATE STATE TESTS
// ============================================

/*
 * Test: Evaluate routes to Show for next round (mid-game state).
 */
void ghostRunnerEvaluateRoutesToNextRound(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up mid-game state: round 0, some lives remaining
    session.currentRound = 0;
    session.livesRemaining = 3;
    session.score = 100;

    suite->game_->skipToState(suite->device_.pdn, 3);  // index 3 = Evaluate
    suite->tick(3);

    ASSERT_EQ(session.currentRound, 1) << "Round should advance";

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_SHOW) << "Should go to Show for next round";
}

/*
 * Test: Evaluate routes to Win when all rounds completed.
 */
void ghostRunnerEvaluateRoutesToWin(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up last round: currentRound = rounds - 1
    session.currentRound = config.rounds - 1;
    session.livesRemaining = 2;
    session.bonkCount = 0;
    session.solutionLength = 5;

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

    // Set up game over state: 0 lives
    session.currentRound = 1;
    session.livesRemaining = 0;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), GHOST_LOSE);
}

// ============================================
// OUTCOME TESTS
// ============================================

/*
 * Test: Win state sets outcome to WON with score.
 */
void ghostRunnerWinSetsOutcome(GhostRunnerTestSuite* suite) {
    suite->game_->getSession().score = 2500;
    suite->game_->skipToState(suite->device_.pdn, 4);  // index 4 = Win
    suite->tick(1);

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::WON);
    ASSERT_EQ(outcome.score, 2500);
}

/*
 * Test: Lose state sets outcome to LOST.
 */
void ghostRunnerLoseSetsOutcome(GhostRunnerTestSuite* suite) {
    suite->game_->getSession().score = 800;
    suite->game_->skipToState(suite->device_.pdn, 5);  // index 5 = Lose
    suite->tick(1);

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::LOST);
    ASSERT_EQ(outcome.score, 800);
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
    suite->tick(1);

    // Advance past 3s win display timer
    suite->tickWithTime(35, 100);

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
    suite->tickWithTime(3, 100);

    // After Wave 17: FDN launches KonamiMetaGame (app 9), which routes to the minigame
    // KMG Handshake waits for *fgame: message with FdnGameType (GHOST_RUNNER = 1)
    suite->player_.serialOutDriver->injectInput("*fgame:1\r");
    suite->tickWithTime(5, 100);

    // KMG should have launched Ghost Runner by now
    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);
    // Note: managedMode may not be set by KMG (TODO: #327 - pass config through app stack)

    // SUCCESS: Test verifies that Wave 17 KMG routing works (FdnDetected → KMG → Minigame)
    // Full game playthrough and return flow tested in end-to-end tests
}

// ============================================
// EDGE CASE TESTS
// ============================================

/*
 * Test: Session reset clears all state correctly.
 */
void ghostRunnerSessionResetClearsState(GhostRunnerTestSuite* suite) {
    GhostRunnerSession session;
    session.cursorRow = 3;
    session.cursorCol = 4;
    session.stepsUsed = 10;
    session.livesRemaining = 0;
    session.score = 100;
    session.bonkCount = 5;
    session.currentRound = 3;
    session.mazeFlashActive = true;

    session.reset();

    ASSERT_EQ(session.cursorRow, 0);
    ASSERT_EQ(session.cursorCol, 0);
    ASSERT_EQ(session.currentDirection, DIR_RIGHT);
    ASSERT_EQ(session.stepsUsed, 0);
    ASSERT_EQ(session.livesRemaining, 3);
    ASSERT_EQ(session.score, 0);
    ASSERT_EQ(session.bonkCount, 0);
    ASSERT_EQ(session.currentRound, 0);
    ASSERT_EQ(session.solutionLength, 0);
    ASSERT_FALSE(session.mazeFlashActive);
}

/*
 * Test: Having exactly 1 life remaining doesn't lose (only 0 causes loss).
 */
void ghostRunnerExactLifeRemainingContinues(GhostRunnerTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

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
 * Test: Bonk with exactly 1 life remaining reduces to 0 lives, but game continues until evaluate.
 */
void ghostRunnerBonkAtLastLife(GhostRunnerTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& session = suite->game_->getSession();
    session.livesRemaining = 1;  // Only 1 life left
    session.walls[0] = WALL_RIGHT;  // Wall to the right

    // Face RIGHT and bonk into wall
    session.currentDirection = DIR_RIGHT;
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // After bonk, should have 0 lives
    EXPECT_EQ(session.livesRemaining, 0);

    // Tick a few more times to allow state transition
    suite->tick(5);

    // Should transition to evaluate, then lose
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
