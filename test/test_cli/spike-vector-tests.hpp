#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// SPIKE VECTOR TEST SUITE (standalone game)
// ============================================

class SpikeVectorTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createGameDevice(0, "spike-vector");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = static_cast<SpikeVector*>(device_.game);

        // Re-enter intro so timers use the correct clock
        // (SimpleTimer clock was null during createGameDevice initialization)
        game_->skipToState(device_.pdn, 0);
        device_.pdn->loop();
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
    SpikeVector* game_ = nullptr;
};

// ============================================
// SPIKE VECTOR MANAGED TEST SUITE (FDN flow)
// ============================================

class SpikeVectorManagedTestSuite : public testing::Test {
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

    SpikeVector* getSpikeVector() {
        return static_cast<SpikeVector*>(
            player_.pdn->getApp(StateId(SPIKE_VECTOR_APP_ID)));
    }

    DeviceInstance player_;
};

// ============================================
// STANDALONE GAME TESTS
// ============================================

/*
 * Test: EASY config preset values are correct.
 */
void spikeVectorEasyConfigPresets(SpikeVectorTestSuite* suite) {
    ASSERT_EQ(SPIKE_VECTOR_EASY.levels, 5);
    ASSERT_EQ(SPIKE_VECTOR_EASY.baseWallCount, 5);
    ASSERT_EQ(SPIKE_VECTOR_EASY.wallCountIncrement, 1);
    ASSERT_EQ(SPIKE_VECTOR_EASY.baseSpeedLevel, 1);
    ASSERT_EQ(SPIKE_VECTOR_EASY.speedLevelIncrement, 1);
    ASSERT_EQ(SPIKE_VECTOR_EASY.baseMaxGapDistance, 2);
    ASSERT_EQ(SPIKE_VECTOR_EASY.numPositions, 5);
    ASSERT_EQ(SPIKE_VECTOR_EASY.startPosition, 2);
    ASSERT_EQ(SPIKE_VECTOR_EASY.hitsAllowed, 3);
    ASSERT_EQ(SPIKE_VECTOR_EASY.wallWidth, 6);
    ASSERT_EQ(SPIKE_VECTOR_EASY.wallSpacing, 14);
}

/*
 * Test: HARD config preset values are correct.
 */
void spikeVectorHardConfigPresets(SpikeVectorTestSuite* suite) {
    ASSERT_EQ(SPIKE_VECTOR_HARD.levels, 5);
    ASSERT_EQ(SPIKE_VECTOR_HARD.baseWallCount, 8);
    ASSERT_EQ(SPIKE_VECTOR_HARD.wallCountIncrement, 1);
    ASSERT_EQ(SPIKE_VECTOR_HARD.baseSpeedLevel, 4);
    ASSERT_EQ(SPIKE_VECTOR_HARD.speedLevelIncrement, 1);
    ASSERT_EQ(SPIKE_VECTOR_HARD.baseMaxGapDistance, 6);
    ASSERT_EQ(SPIKE_VECTOR_HARD.numPositions, 7);
    ASSERT_EQ(SPIKE_VECTOR_HARD.startPosition, 3);
    ASSERT_EQ(SPIKE_VECTOR_HARD.hitsAllowed, 1);
}

/*
 * Test: Intro resets all session fields.
 * Dirties session fields, then enters intro to verify reset.
 */
void spikeVectorIntroResetsSession(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();

    // Dirty all session fields
    session.cursorPosition = 4;
    session.currentLevel = 3;
    session.hits = 2;
    session.score = 300;
    session.gapPositions.push_back(1);
    session.gapPositions.push_back(2);
    session.formationX = 50;
    session.nextWallIndex = 2;
    session.levelComplete = true;

    // Skip to intro (index 0) to trigger reset
    suite->game_->skipToState(suite->device_.pdn, 0);
    suite->device_.pdn->loop();

    ASSERT_EQ(session.cursorPosition, 2);
    ASSERT_EQ(session.currentLevel, 0);
    ASSERT_EQ(session.hits, 0);
    ASSERT_EQ(session.score, 0);
    ASSERT_TRUE(session.gapPositions.empty());
    ASSERT_EQ(session.formationX, 128);
    ASSERT_EQ(session.nextWallIndex, 0);
    ASSERT_FALSE(session.levelComplete);
}

/*
 * Test: Intro timer transitions to Show state.
 */
void spikeVectorIntroTransitionsToShow(SpikeVectorTestSuite* suite) {
    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_INTRO);

    // Advance past 2s intro timer
    suite->tickWithTime(25, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_SHOW);
}

/*
 * Test: Show state generates gap positions array.
 */
void spikeVectorShowGeneratesGaps(SpikeVectorTestSuite* suite) {
    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_SHOW);

    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    int expectedWalls = wallsForLevel(config, session.currentLevel);
    ASSERT_EQ((int)session.gapPositions.size(), expectedWalls);

    // All gaps should be within valid range
    for (int gap : session.gapPositions) {
        ASSERT_GE(gap, 0);
        ASSERT_LT(gap, config.numPositions);
    }
}

/*
 * Test: Show timer transitions to Gameplay state.
 */
void spikeVectorShowTransitionsToGameplay(SpikeVectorTestSuite* suite) {
    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_SHOW);

    // Advance past 1000ms show timer
    suite->tickWithTime(15, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_GAMEPLAY);
}

/*
 * Test: Formation advances left during Gameplay.
 */
void spikeVectorFormationAdvances(SpikeVectorTestSuite* suite) {
    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_GAMEPLAY);

    auto& session = suite->game_->getSession();
    int initialX = session.formationX;

    // Advance time — formation moves left 1px per speed tick
    suite->tickWithTime(10, speedMsForLevel(suite->game_->getConfig(), 0));

    ASSERT_LT(session.formationX, initialX);
}

/*
 * Test: Correct dodge at gap position earns score and no hit.
 */
void spikeVectorCorrectDodge(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Use deterministic seed for consistent gaps
    config.rngSeed = 42;
    suite->game_->seedRng(config.rngSeed);

    // Skip to Show (index 1) to generate gaps
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    // Get the first wall's gap position
    int firstGap = session.gapPositions[0];

    // Advance past show timer to gameplay
    suite->tickWithTime(15, 100);
    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_GAMEPLAY);

    // Move cursor to match gap position using button presses
    while (session.cursorPosition > firstGap) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (session.cursorPosition < firstGap) {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    ASSERT_EQ(session.cursorPosition, firstGap);

    int hitsBefore = session.hits;
    int scoreBefore = session.score;

    // Advance until first wall reaches cursor (formationX ~= 8)
    int speedMs = speedMsForLevel(config, 0);
    suite->tickWithTime(130 / speedMs + 5, speedMs);

    // Should have dodged (score increased, hits unchanged)
    ASSERT_GT(session.score, scoreBefore);
    ASSERT_EQ(session.hits, hitsBefore);
}

/*
 * Test: Missing the gap position registers a hit.
 */
void spikeVectorMissedDodge(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Use deterministic seed
    config.rngSeed = 123;
    suite->game_->seedRng(config.rngSeed);

    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    int firstGap = session.gapPositions[0];

    // Advance past show timer to gameplay
    suite->tickWithTime(15, 100);
    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_GAMEPLAY);

    // Move cursor to a position that is NOT the gap
    int wrongPos = (firstGap + 1) % config.numPositions;
    while (session.cursorPosition > wrongPos) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (session.cursorPosition < wrongPos) {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    ASSERT_NE(session.cursorPosition, firstGap);

    int hitsBefore = session.hits;

    // Advance until first wall reaches cursor
    int speedMs = speedMsForLevel(config, 0);
    suite->tickWithTime(130 / speedMs + 5, speedMs);

    // Hits should have increased
    ASSERT_GT(session.hits, hitsBefore);
}

/*
 * Test: Formation fully passing off-screen causes transition to Evaluate.
 */
void spikeVectorFormationCompleteTransition(SpikeVectorTestSuite* suite) {
    auto& config = suite->game_->getConfig();

    // Use minimal walls for faster test
    config.baseWallCount = 2;

    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_GAMEPLAY);

    // Advance enough time for all walls to pass off-screen
    // formationX starts at 128, needs to reach ~-26 (2 walls * 20 + 6)
    int speedMs = speedMsForLevel(config, 0);
    suite->tickWithTime(160 / speedMs + 10, speedMs);

    // Should have transitioned to Evaluate or beyond
    int stateId = suite->game_->getCurrentStateId();
    ASSERT_NE(stateId, SPIKE_GAMEPLAY);
}

/*
 * Test: Evaluate routes to Show for next level (mid-game state).
 */
void spikeVectorEvaluateRoutesToNextLevel(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up mid-game state: level 2 of 5, no excess hits
    session.currentLevel = 2;
    session.hits = 0;

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->device_.pdn->loop();

    // Evaluate should route to Show for the next level
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_SHOW);
}

/*
 * Test: Evaluate routes to Win when all levels completed.
 */
void spikeVectorEvaluateRoutesToWin(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up last level: currentLevel = levels - 1
    session.currentLevel = config.levels - 1;
    session.hits = 0;

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->device_.pdn->loop();
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_WIN);
}

/*
 * Test: Evaluate routes to Lose when hits exceed allowed.
 */
void spikeVectorEvaluateRoutesToLose(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up over-hit state
    session.currentLevel = 1;
    session.hits = config.hitsAllowed + 1;  // Over the limit

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->device_.pdn->loop();
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_LOSE);
}

/*
 * Test: Win state sets outcome with WON result and actual score.
 */
void spikeVectorWinSetsOutcome(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.score = 500;

    // Skip to Win (index 4)
    suite->game_->skipToState(suite->device_.pdn, 4);
    suite->device_.pdn->loop();

    auto& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::WON);
    ASSERT_EQ(outcome.score, 500);
}

/*
 * Test: Lose state sets outcome with LOST result.
 */
void spikeVectorLoseSetsOutcome(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.score = 200;

    // Skip to Lose (index 5)
    suite->game_->skipToState(suite->device_.pdn, 5);
    suite->device_.pdn->loop();

    auto& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::LOST);
    ASSERT_EQ(outcome.score, 200);
}

/*
 * Test: In standalone mode, Win loops back to Intro after timer.
 */
void spikeVectorStandaloneLoopsToIntro(SpikeVectorTestSuite* suite) {
    // Skip to Win (index 4)
    suite->game_->skipToState(suite->device_.pdn, 4);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_WIN);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), SPIKE_INTRO);
}

/*
 * Test: All 6 state names resolve correctly.
 */
void spikeVectorStateNamesResolve(SpikeVectorTestSuite* suite) {
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_INTRO), "SpikeVectorIntro");
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_WIN), "SpikeVectorWin");
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_LOSE), "SpikeVectorLose");
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_SHOW), "SpikeVectorShow");
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_GAMEPLAY), "SpikeVectorGameplay");
    ASSERT_STREQ(getSpikeVectorStateName(SPIKE_EVALUATE), "SpikeVectorEvaluate");

    // Verify getStateName routes correctly for spike vector IDs
    ASSERT_STREQ(getStateName(SPIKE_INTRO), "SpikeVectorIntro");
    ASSERT_STREQ(getStateName(SPIKE_WIN), "SpikeVectorWin");
    ASSERT_STREQ(getStateName(SPIKE_SHOW), "SpikeVectorShow");
    ASSERT_STREQ(getStateName(SPIKE_GAMEPLAY), "SpikeVectorGameplay");
    ASSERT_STREQ(getStateName(SPIKE_EVALUATE), "SpikeVectorEvaluate");
}

// ============================================
// MANAGED MODE TEST (FDN flow)
// ============================================

/*
 * Test: Full FDN flow — idle -> fdn handshake -> spike vector -> play through -> win -> FdnComplete.
 */
void spikeVectorManagedModeReturns(SpikeVectorManagedTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN handshake for Spike Vector (GameType 2, KonamiButton DOWN=1)
    suite->player_.serialOutDriver->injectInput("*fdn:2:1\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Should be in Spike Vector Intro now
    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);
    ASSERT_TRUE(sv->getConfig().managedMode);
    ASSERT_EQ(sv->getCurrentStateId(), SPIKE_INTRO);

    // Reduce level count for faster test
    sv->getConfig().levels = 2;
    sv->getConfig().baseWallCount = 2;

    // Play through all levels by letting time advance
    auto& session = sv->getSession();

    for (int attempt = 0; attempt < 150; attempt++) {
        int stateId = sv->getCurrentStateId();
        if (stateId == SPIKE_WIN || stateId == SPIKE_LOSE) break;

        if (stateId == SPIKE_INTRO) {
            suite->tickWithTime(1, 2100);
            continue;
        }

        if (stateId == SPIKE_SHOW) {
            // Move to first gap before gameplay starts
            if (!session.gapPositions.empty()) {
                int firstGap = session.gapPositions[0];
                suite->tickWithTime(1, 1100);
                // Now in gameplay, move cursor
                if (sv->getCurrentStateId() == SPIKE_GAMEPLAY) {
                    while (session.cursorPosition > firstGap) {
                        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    }
                    while (session.cursorPosition < firstGap) {
                        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    }
                }
            }
            continue;
        }

        if (stateId == SPIKE_GAMEPLAY) {
            // Let formation pass
            int speedMs = speedMsForLevel(sv->getConfig(), session.currentLevel);
            suite->tickWithTime(10, speedMs);
            continue;
        }

        // For evaluate or any other state, just tick
        suite->tickWithTime(1, 10);
    }

    // Should be in Win
    ASSERT_EQ(sv->getCurrentStateId(), SPIKE_WIN);
    ASSERT_EQ(sv->getOutcome().result, MiniGameResult::WON);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

// ============================================
// EDGE CASE TESTS (simplified for new API)
// ============================================

/*
 * Test: Cursor at position 0 (bottom boundary) stays at 0 when moving up.
 */
void spikeVectorCursorBottomBoundaryClamp(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.cursorPosition = 0;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Try to move below 0
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);

    EXPECT_EQ(session.cursorPosition, 0) << "Cursor should not move below 0";
}

/*
 * Test: Cursor at max position (top boundary) stays at max when moving down.
 */
void spikeVectorCursorTopBoundaryClamp(SpikeVectorTestSuite* suite) {
    auto& config = suite->game_->getConfig();
    auto& session = suite->game_->getSession();
    int maxPos = config.numPositions - 1;
    session.cursorPosition = maxPos;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Try to move above max
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);

    EXPECT_EQ(session.cursorPosition, maxPos) << "Cursor should not move above numPositions-1";
}

/*
 * Test: Level progression — currentLevel increments after each Evaluate.
 */
void spikeVectorLevelProgressionIncrement(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    session.currentLevel = 0;
    session.hits = 0;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    EXPECT_EQ(session.currentLevel, 1) << "currentLevel should increment after Evaluate";

    session.hits = 0;
    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    EXPECT_EQ(session.currentLevel, 2) << "currentLevel should increment again";
}

/*
 * Test: Deterministic RNG with seed produces consistent gap patterns.
 */
void spikeVectorDeterministicRngGapPattern(SpikeVectorTestSuite* suite) {
    auto& config = suite->game_->getConfig();
    config.rngSeed = 12345;
    suite->game_->seedRng(config.rngSeed);

    std::vector<std::vector<int>> gaps1;
    for (int i = 0; i < 3; i++) {
        suite->game_->skipToState(suite->device_.pdn, 1);  // Show
        suite->tick(1);
        gaps1.push_back(suite->game_->getSession().gapPositions);
    }

    // Reset and re-seed
    config.rngSeed = 12345;
    suite->game_->seedRng(config.rngSeed);

    std::vector<std::vector<int>> gaps2;
    for (int i = 0; i < 3; i++) {
        suite->game_->skipToState(suite->device_.pdn, 1);  // Show
        suite->tick(1);
        gaps2.push_back(suite->game_->getSession().gapPositions);
    }

    EXPECT_EQ(gaps1, gaps2) << "Same seed should produce identical gap sequences";
}

/*
 * Test: Losing on the final level still routes to Lose (not Win).
 */
void spikeVectorLoseOnFinalLevel(SpikeVectorTestSuite* suite) {
    auto& config = suite->game_->getConfig();
    auto& session = suite->game_->getSession();

    config.levels = 3;
    config.hitsAllowed = 1;

    // Final level, about to evaluate with too many hits
    session.currentLevel = 2;  // levels-1
    session.hits = 2;          // over limit

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    EXPECT_EQ(suite->game_->getCurrentStateId(), SPIKE_LOSE)
        << "Should lose even on final level if hits exceed allowed";
}

#endif // NATIVE_BUILD
