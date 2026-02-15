#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
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
        player_.game->skipToState(player_.pdn, 7);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentState()->getStateId();
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
    ASSERT_EQ(SPIKE_VECTOR_EASY.approachSpeedMs, 40);
    ASSERT_EQ(SPIKE_VECTOR_EASY.waves, 5);
    ASSERT_EQ(SPIKE_VECTOR_EASY.hitsAllowed, 3);
    ASSERT_EQ(SPIKE_VECTOR_EASY.numPositions, 5);
    ASSERT_EQ(SPIKE_VECTOR_EASY.trackLength, 100);
    ASSERT_EQ(SPIKE_VECTOR_EASY.startPosition, 2);
}

/*
 * Test: HARD config preset values are correct.
 */
void spikeVectorHardConfigPresets(SpikeVectorTestSuite* suite) {
    ASSERT_EQ(SPIKE_VECTOR_HARD.approachSpeedMs, 20);
    ASSERT_EQ(SPIKE_VECTOR_HARD.waves, 8);
    ASSERT_EQ(SPIKE_VECTOR_HARD.hitsAllowed, 1);
    ASSERT_EQ(SPIKE_VECTOR_HARD.numPositions, 7);
    ASSERT_EQ(SPIKE_VECTOR_HARD.startPosition, 3);
}

/*
 * Test: Intro resets all session fields.
 * Dirties session fields, then enters intro to verify reset.
 */
void spikeVectorIntroResetsSession(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();

    // Dirty all session fields
    session.cursorPosition = 4;
    session.wallPosition = 50;
    session.gapPosition = 3;
    session.currentWave = 3;
    session.hits = 2;
    session.score = 300;
    session.wallArrived = true;

    // Skip to intro (index 0) to trigger reset
    suite->game_->skipToState(suite->device_.pdn, 0);
    suite->device_.pdn->loop();

    ASSERT_EQ(session.cursorPosition, 2);
    ASSERT_EQ(session.wallPosition, 0);
    ASSERT_EQ(session.gapPosition, 0);
    ASSERT_EQ(session.currentWave, 0);
    ASSERT_EQ(session.hits, 0);
    ASSERT_EQ(session.score, 0);
    ASSERT_FALSE(session.wallArrived);
}

/*
 * Test: Intro timer transitions to Show state.
 */
void spikeVectorIntroTransitionsToShow(SpikeVectorTestSuite* suite) {
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_INTRO);

    // Advance past 2s intro timer
    suite->tickWithTime(25, 100);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_SHOW);
}

/*
 * Test: Show state is accessible and has correct ID.
 */
void spikeVectorShowDisplaysWaveInfo(SpikeVectorTestSuite* suite) {
    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_SHOW);
}

/*
 * Test: Show timer transitions to Gameplay state.
 */
void spikeVectorShowTransitionsToGameplay(SpikeVectorTestSuite* suite) {
    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_SHOW);

    // Advance past 1500ms show timer
    suite->tickWithTime(20, 100);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_GAMEPLAY);
}

/*
 * Test: Wall position advances with time during Gameplay.
 */
void spikeVectorWallAdvancesWithTime(SpikeVectorTestSuite* suite) {
    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_GAMEPLAY);

    auto& session = suite->game_->getSession();
    int initialWall = session.wallPosition;

    // Advance time — each tick of approachSpeedMs (40ms) moves wall 1 step
    suite->tickWithTime(5, 50);

    ASSERT_GT(session.wallPosition, initialWall);
}

/*
 * Test: Correct dodge at gap position earns score and no hit.
 */
void spikeVectorCorrectDodgeAtGap(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Use a short track for quick test
    config.trackLength = 5;

    // Skip to Show (index 1) to set up gap position
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    // Now in Show — the gap was generated. Get the gap position.
    int gap = session.gapPosition;

    // Advance past show timer to gameplay
    suite->tickWithTime(20, 100);
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_GAMEPLAY);

    // Move cursor to match gap position using button presses
    // Primary = UP (decrease), Secondary = DOWN (increase)
    while (session.cursorPosition > gap) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (session.cursorPosition < gap) {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    ASSERT_EQ(session.cursorPosition, gap);

    int hitsBefore = session.hits;
    int scoreBefore = session.score;

    // Advance time until wall arrives (short track = 5 steps at 40ms each)
    suite->tickWithTime(20, 50);

    // Should be in Evaluate or past it
    // Score should have increased, hits should not
    ASSERT_GT(session.score, scoreBefore);
    ASSERT_EQ(session.hits, hitsBefore);
}

/*
 * Test: Missing the gap position registers a hit.
 */
void spikeVectorMissedDodge(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Use deterministic seed and short track
    config.rngSeed = 42;
    config.trackLength = 5;
    suite->game_->seedRng();

    // Skip to Show (index 1)
    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->device_.pdn->loop();

    int gap = session.gapPosition;

    // Advance past show timer to gameplay
    suite->tickWithTime(20, 100);
    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_GAMEPLAY);

    // Move cursor to a position that is NOT the gap
    int wrongPos = (gap + 1) % config.numPositions;
    while (session.cursorPosition > wrongPos) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (session.cursorPosition < wrongPos) {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    ASSERT_NE(session.cursorPosition, gap);

    int hitsBefore = session.hits;

    // Advance time until wall arrives
    suite->tickWithTime(20, 50);

    // Hits should have increased
    ASSERT_GT(session.hits, hitsBefore);
}

/*
 * Test: Wall reaching trackLength causes transition to Evaluate.
 */
void spikeVectorWallTimeoutCausesEvaluate(SpikeVectorTestSuite* suite) {
    auto& config = suite->game_->getConfig();

    // Use very short track
    config.trackLength = 3;

    // Skip to Gameplay (index 2)
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->device_.pdn->loop();

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_GAMEPLAY);

    // Advance enough time for wall to reach end (3 steps at 40ms each)
    suite->tickWithTime(10, 50);

    // Should have transitioned through Evaluate to Show or Win/Lose
    int stateId = suite->game_->getCurrentState()->getStateId();
    ASSERT_NE(stateId, SPIKE_GAMEPLAY);
}

/*
 * Test: Evaluate routes to Show for next wave (mid-game state).
 */
void spikeVectorEvaluateRoutesToNextWave(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();

    // Set up mid-game state: wave 2 of 5, no excess hits
    session.currentWave = 2;
    session.hits = 0;
    session.cursorPosition = 0;
    session.gapPosition = 0;  // cursor == gap, so it's a dodge

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->device_.pdn->loop();

    // Evaluate should route to Show for the next wave
    // The transition happens in onStateMounted, then the state machine
    // picks it up on the next loop
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_SHOW);
}

/*
 * Test: Evaluate routes to Win when all waves completed.
 */
void spikeVectorEvaluateRoutesToWin(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up last wave: currentWave = waves - 1 (about to complete final wave)
    session.currentWave = config.waves - 1;
    session.hits = 0;
    session.cursorPosition = 0;
    session.gapPosition = 0;  // dodge

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->device_.pdn->loop();
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_WIN);
}

/*
 * Test: Evaluate routes to Lose when hits exceed allowed.
 */
void spikeVectorEvaluateRoutesToLose(SpikeVectorTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    // Set up over-hit state: cursor != gap to trigger another hit
    session.currentWave = 1;
    session.hits = config.hitsAllowed;  // Already at limit
    session.cursorPosition = 0;
    session.gapPosition = 1;  // miss — this will push hits over

    // Skip to Evaluate (index 3)
    suite->game_->skipToState(suite->device_.pdn, 3);
    suite->device_.pdn->loop();
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_LOSE);
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

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_WIN);

    // Advance past win timer (3s)
    suite->tickWithTime(35, 100);

    ASSERT_EQ(suite->game_->getCurrentState()->getStateId(), SPIKE_INTRO);
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
 * Uses FDN GameType 2, KonamiButton DOWN=1.
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
    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_INTRO);

    // Use a short track for quick playthrough
    sv->getConfig().trackLength = 3;

    // Play through all waves by advancing to each state and handling it
    auto& session = sv->getSession();

    for (int attempt = 0; attempt < 100; attempt++) {
        int stateId = sv->getCurrentState()->getStateId();
        if (stateId == SPIKE_WIN || stateId == SPIKE_LOSE) break;

        if (stateId == SPIKE_INTRO) {
            // Advance past intro timer (2s)
            suite->tickWithTime(1, 2100);
            continue;
        }

        if (stateId == SPIKE_SHOW) {
            int gap = session.gapPosition;
            // Advance past show timer (1.5s)
            suite->tickWithTime(1, 1600);
            // Now should be in Gameplay — move cursor to gap
            if (sv->getCurrentState()->getStateId() == SPIKE_GAMEPLAY) {
                while (session.cursorPosition > gap) {
                    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                }
                while (session.cursorPosition < gap) {
                    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                }
            }
            continue;
        }

        if (stateId == SPIKE_GAMEPLAY) {
            // Advance wall to end (3 steps at 40ms each, use generous time)
            suite->tickWithTime(10, 50);
            continue;
        }

        // For evaluate or any other state, just tick
        suite->tickWithTime(1, 10);
    }

    // Should be in Win
    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_WIN);
    ASSERT_EQ(sv->getOutcome().result, MiniGameResult::WON);

    // Advance past win timer (3s) — use multiple ticks for state machine processing
    suite->tickWithTime(35, 100);

    // Should return to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

#endif // NATIVE_BUILD
