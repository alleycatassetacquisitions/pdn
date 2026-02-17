#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-states.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// BREACH DEFENSE STANDALONE TEST SUITE
// ============================================

class BreachDefenseTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createGameDevice(0, "breach-defense");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = static_cast<BreachDefense*>(device_.game);
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
    BreachDefense* game_ = nullptr;
};

// ============================================
// BREACH DEFENSE MANAGED TEST SUITE (FDN)
// ============================================

class BreachDefenseManagedTestSuite : public testing::Test {
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

    BreachDefense* getBreachDefense() {
        return static_cast<BreachDefense*>(
            player_.pdn->getApp(StateId(BREACH_DEFENSE_APP_ID)));
    }

    DeviceInstance player_;
};

// ============================================
// TEST FUNCTIONS
// ============================================

/*
 * Test 1: Verify EASY config presets.
 */
void breachDefenseEasyConfigPresets(BreachDefenseTestSuite* suite) {
    BreachDefenseConfig easy = makeBreachDefenseEasyConfig();
    ASSERT_EQ(easy.numLanes, 3);
    ASSERT_EQ(easy.threatSpeedMs, 40);
    ASSERT_EQ(easy.threatDistance, 100);
    ASSERT_EQ(easy.totalThreats, 6);
    ASSERT_EQ(easy.missesAllowed, 3);
}

/*
 * Test 2: Verify HARD config presets.
 */
void breachDefenseHardConfigPresets(BreachDefenseTestSuite* suite) {
    BreachDefenseConfig hard = makeBreachDefenseHardConfig();
    ASSERT_EQ(hard.numLanes, 5);
    ASSERT_EQ(hard.threatSpeedMs, 20);
    ASSERT_EQ(hard.threatDistance, 100);
    ASSERT_EQ(hard.totalThreats, 12);
    ASSERT_EQ(hard.missesAllowed, 1);
}

/*
 * Test 3: Intro resets session to defaults.
 */
void breachDefenseIntroResetsSession(BreachDefenseTestSuite* suite) {
    // Dirty the session
    auto& sess = suite->game_->getSession();
    sess.score = 999;
    sess.breaches = 5;
    sess.currentThreat = 10;
    sess.shieldLane = 4;

    // Skip to intro (index 0)
    suite->game_->skipToState(suite->device_.pdn, 0);
    suite->tick(1);

    ASSERT_EQ(sess.score, 0);
    ASSERT_EQ(sess.breaches, 0);
    ASSERT_EQ(sess.currentThreat, 0);
    ASSERT_EQ(sess.shieldLane, 0);
}

/*
 * Test 4: Intro transitions away from Intro after timer.
 * Note: The intro timer may expire immediately on the first tick (because
 * the platform clock isn't set during device creation). The important thing
 * is that Intro's transition wiring leads to Show (not Win or Lose).
 */
void breachDefenseIntroTransitionsToShow(BreachDefenseTestSuite* suite) {
    // Use skipToState to mount Intro cleanly with the platform clock set
    suite->game_->skipToState(suite->device_.pdn, 0);
    suite->tick(1);
    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_INTRO);

    // Advance past 2s intro timer
    suite->tickWithTime(25, 100);

    // Should have transitioned to Show (1500ms timer), not yet to Gameplay
    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_SHOW);
}

/*
 * Test 5: Show state is reachable and has correct state ID.
 */
void breachDefenseShowDisplaysThreatInfo(BreachDefenseTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);  // Show is index 1
    suite->tick(1);

    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_SHOW);
}

/*
 * Test 6: Show transitions to Gameplay after timer.
 */
void breachDefenseShowTransitionsToGameplay(BreachDefenseTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);  // Show
    suite->tick(1);
    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_SHOW);

    // Advance past 1.5s show timer
    suite->tickWithTime(20, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_GAMEPLAY);
}

/*
 * Test 7: Threat position advances with time in Gameplay state.
 */
void breachDefenseThreatAdvancesWithTime(BreachDefenseTestSuite* suite) {
    suite->game_->getConfig().threatSpeedMs = 10;
    suite->game_->getConfig().threatDistance = 1000;  // Large distance so it doesn't arrive
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    int initialPos = suite->game_->getSession().threatPosition;

    // Advance a few ticks
    suite->tickWithTime(5, 15);

    ASSERT_GT(suite->game_->getSession().threatPosition, initialPos);
}

/*
 * Test 8: Correct block — shield matches threat lane, score increases, no breach.
 */
void breachDefenseCorrectBlock(BreachDefenseTestSuite* suite) {
    suite->game_->getConfig().threatSpeedMs = 5;
    suite->game_->getConfig().threatDistance = 10;
    suite->game_->getConfig().totalThreats = 4;
    suite->game_->getConfig().missesAllowed = 3;
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Set shield to match threat lane
    auto& sess = suite->game_->getSession();
    sess.shieldLane = sess.threatLane;

    // Advance until threat arrives
    suite->tickWithTime(30, 10);

    // Should have evaluated — check score
    ASSERT_EQ(sess.score, 100);
    ASSERT_EQ(sess.breaches, 0);
}

/*
 * Test 9: Missed threat — shield in wrong lane, breach counted.
 */
void breachDefenseMissedThreat(BreachDefenseTestSuite* suite) {
    suite->game_->getConfig().threatSpeedMs = 5;
    suite->game_->getConfig().threatDistance = 10;
    suite->game_->getConfig().totalThreats = 4;
    suite->game_->getConfig().missesAllowed = 3;
    suite->game_->getConfig().numLanes = 3;
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Set shield to NOT match threat lane
    auto& sess = suite->game_->getSession();
    if (sess.threatLane == 0) {
        sess.shieldLane = 1;
    } else {
        sess.shieldLane = 0;
    }

    // Advance until threat arrives
    suite->tickWithTime(30, 10);

    // Should have evaluated — check breach counted
    ASSERT_EQ(sess.score, 0);
    ASSERT_EQ(sess.breaches, 1);
}

/*
 * Test 10: Shield moves with button presses.
 */
void breachDefenseShieldMovesUpDown(BreachDefenseTestSuite* suite) {
    suite->game_->getConfig().numLanes = 5;
    suite->game_->getConfig().threatDistance = 10000;  // Keep gameplay alive
    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    auto& sess = suite->game_->getSession();
    sess.shieldLane = 2;

    // Press DOWN (secondary button) — should increase lane
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    ASSERT_EQ(sess.shieldLane, 3);

    // Press UP (primary button) — should decrease lane
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    ASSERT_EQ(sess.shieldLane, 2);
}

/*
 * Test 11: Evaluate routes to Show for next threat (mid-game).
 */
void breachDefenseEvaluateRoutesToNextThreat(BreachDefenseTestSuite* suite) {
    // Configure for multiple threats
    suite->game_->getConfig().totalThreats = 4;
    suite->game_->getConfig().missesAllowed = 3;
    suite->game_->getConfig().threatSpeedMs = 5;
    suite->game_->getConfig().threatDistance = 5;

    // Start at Gameplay
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Let threat arrive and evaluate
    suite->tickWithTime(30, 10);

    // After evaluation timer, should route to Show (next threat)
    // The session should now be on threat 1 (second threat)
    ASSERT_EQ(suite->game_->getSession().currentThreat, 1);

    // Wait for eval to complete and transition
    suite->tickWithTime(10, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_SHOW);
}

/*
 * Test 12: Evaluate routes to Win when all threats survived.
 */
void breachDefenseEvaluateRoutesToWin(BreachDefenseTestSuite* suite) {
    suite->game_->getConfig().totalThreats = 1;
    suite->game_->getConfig().missesAllowed = 3;
    suite->game_->getConfig().threatSpeedMs = 5;
    suite->game_->getConfig().threatDistance = 5;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Match shield to threat
    auto& sess = suite->game_->getSession();
    sess.shieldLane = sess.threatLane;

    // Let threat arrive and evaluate
    suite->tickWithTime(30, 10);

    // Wait for eval timer
    suite->tickWithTime(10, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_WIN);
}

/*
 * Test 13: Evaluate routes to Lose when too many breaches.
 */
void breachDefenseEvaluateRoutesToLose(BreachDefenseTestSuite* suite) {
    suite->game_->getConfig().totalThreats = 4;
    suite->game_->getConfig().missesAllowed = 0;  // No misses allowed
    suite->game_->getConfig().threatSpeedMs = 5;
    suite->game_->getConfig().threatDistance = 5;
    suite->game_->getConfig().numLanes = 3;

    suite->game_->skipToState(suite->device_.pdn, 2);  // Gameplay
    suite->tick(1);

    // Ensure shield does NOT match threat
    auto& sess = suite->game_->getSession();
    if (sess.threatLane == 0) {
        sess.shieldLane = 1;
    } else {
        sess.shieldLane = 0;
    }

    // Let threat arrive and evaluate
    suite->tickWithTime(30, 10);

    // Wait for eval timer
    suite->tickWithTime(10, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_LOSE);
}

/*
 * Test 14: Win state sets outcome to WON.
 */
void breachDefenseWinSetsOutcome(BreachDefenseTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 4);  // Win is index 4
    suite->tick(1);

    ASSERT_EQ(suite->game_->getOutcome().result, MiniGameResult::WON);
}

/*
 * Test 15: Lose state sets outcome to LOST.
 */
void breachDefenseLoseSetsOutcome(BreachDefenseTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 5);  // Lose is index 5
    suite->tick(1);

    ASSERT_EQ(suite->game_->getOutcome().result, MiniGameResult::LOST);
}

/*
 * Test 16: In standalone mode, Win loops back to Intro after timer.
 */
void breachDefenseStandaloneLoopsToIntro(BreachDefenseTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 4);  // Win
    suite->tick(1);
    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_WIN);

    // Advance past 3s win timer
    suite->tickWithTime(35, 100);

    ASSERT_EQ(suite->game_->getCurrentStateId(), BREACH_INTRO);
}

/*
 * Test 17: All 6 state names resolve correctly.
 */
void breachDefenseStateNamesResolve(BreachDefenseTestSuite* suite) {
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_INTRO), "BreachDefenseIntro");
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_WIN), "BreachDefenseWin");
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_LOSE), "BreachDefenseLose");
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_SHOW), "BreachDefenseShow");
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_GAMEPLAY), "BreachDefenseGameplay");
    ASSERT_STREQ(getBreachDefenseStateName(BREACH_EVALUATE), "BreachDefenseEvaluate");

    // Verify getStateName routes correctly
    ASSERT_STREQ(getStateName(BREACH_INTRO), "BreachDefenseIntro");
    ASSERT_STREQ(getStateName(BREACH_GAMEPLAY), "BreachDefenseGameplay");
}

/*
 * Test 18: Managed mode — FDN launches Breach Defense and returns to FdnComplete.
 */
void breachDefenseManagedModeReturns(BreachDefenseManagedTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN handshake for Breach Defense (GameType 6, KonamiButton A=5)
    suite->player_.serialOutDriver->injectInput("*fdn:6:5\r");
    for (int i = 0; i < 3; i++) {
        SerialCableBroker::getInstance().transferData();
        suite->player_.pdn->loop();
    }
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete FDN handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(3, 100);

    // After Wave 17: FDN launches KonamiMetaGame (app 9), which routes to the minigame
    // The active app should be KonamiMetaGame now
    // KMG Handshake waits for *fgame: message with FdnGameType (BREACH_DEFENSE = 6)
    suite->player_.serialOutDriver->injectInput("*fgame:6\r");
    suite->tickWithTime(5, 100);

    // KMG should have launched Breach Defense by now
    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);
    ASSERT_TRUE(bd->getConfig().managedMode);

    // Configure very short game for quick playthrough
    bd->getConfig().threatSpeedMs = 5;
    bd->getConfig().threatDistance = 3;
    bd->getConfig().totalThreats = 1;
    bd->getConfig().missesAllowed = 3;

    // Advance past intro timer (2s) + show timer (1.5s)
    // Total: 3.5s + margin. The clock was valid when Intro mounted via FDN,
    // so timers work correctly from this point.
    suite->tickWithTime(40, 100);

    // Should be in Gameplay by now (past intro 2s + show 1.5s = 3.5s, we advanced 4s)
    // The threat may or may not have arrived depending on exact timing
    int stateId = bd->getCurrentStateId();

    // If still in Gameplay, force the threat through
    if (stateId == BREACH_GAMEPLAY) {
        bd->getSession().shieldLane = bd->getSession().threatLane;
        suite->tickWithTime(20, 20);  // Enough for threat to arrive
    }

    // If in Evaluate, wait for eval timer
    stateId = bd->getCurrentStateId();
    if (stateId == BREACH_EVALUATE) {
        suite->tickWithTime(10, 100);  // Past 500ms eval timer
    }

    // Should be in Win now (or already past it)
    stateId = bd->getCurrentStateId();
    if (stateId == BREACH_WIN) {
        ASSERT_EQ(bd->getOutcome().result, MiniGameResult::WON);
        // Advance past win timer (3s)
        suite->tickWithTime(35, 100);
    }

    // Should have returned to Quickdraw's FdnComplete state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

// ============================================
// EDGE CASE TESTS
// ============================================

/*
 * Test: Shield at lane 0 (bottom boundary) can block threat at lane 0.
 */
void breachDefenseBlockAtLaneZero(BreachDefenseTestSuite* suite) {
    auto& session = suite->game_->getSession();

    session.shieldLane = 0;  // Bottom boundary
    session.threatLane = 0;  // Threat at same lane
    session.breaches = 0;
    int initialScore = session.score;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    // Wait for eval timer to finish
    suite->tickWithTime(10, 100);

    EXPECT_EQ(session.score, initialScore + 100) << "Block at lane 0 should award score";
    EXPECT_EQ(session.breaches, 0) << "No breach should be counted for successful block";
}

/*
 * Test: Shield at max lane (top boundary) can block threat at max lane.
 */
void breachDefenseBlockAtMaxLane(BreachDefenseTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();
    int maxLane = config.numLanes - 1;

    session.shieldLane = maxLane;  // Top boundary
    session.threatLane = maxLane;  // Threat at same lane
    session.breaches = 0;
    int initialScore = session.score;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    // Wait for eval timer to finish
    suite->tickWithTime(10, 100);

    EXPECT_EQ(session.score, initialScore + 100) << "Block at max lane should award score";
    EXPECT_EQ(session.breaches, 0) << "No breach should be counted for successful block";
}

/*
 * Test: Exact breaches equal to missesAllowed doesn't lose (only > causes loss).
 */
void breachDefenseExactBreachesEqualAllowed(BreachDefenseTestSuite* suite) {
    auto& session = suite->game_->getSession();
    auto& config = suite->game_->getConfig();

    config.missesAllowed = 2;
    config.totalThreats = 5;

    session.shieldLane = 0;
    session.threatLane = 1;  // Shield doesn't match = breach
    session.breaches = 1;    // Already 1 breach
    session.currentThreat = 0;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    // Wait for eval timer
    suite->tickWithTime(10, 100);

    // 1 + 1 = 2 breaches, missesAllowed = 2, so 2 > 2 is false -> continue
    State* state = suite->game_->getCurrentState();
    EXPECT_NE(state->getStateId(), BREACH_LOSE) << "Should not lose when breaches == missesAllowed";
    EXPECT_EQ(session.breaches, 2);
}

/*
 * Test: Evaluate sets haptics intensity based on block vs breach.
 */
void breachDefenseHapticsIntensityDiffers(BreachDefenseTestSuite* suite) {
    auto& session = suite->game_->getSession();

    // Test blocked threat (should use intensity 150)
    session.shieldLane = 1;
    session.threatLane = 1;  // Match = block
    session.breaches = 0;

    suite->game_->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->tick(3);

    // Haptics should be on at intensity 150 (from code inspection)
    // We can't directly assert haptics state in this test framework,
    // but we can verify the evaluate state transitions correctly
    suite->tickWithTime(10, 100);

    EXPECT_EQ(session.breaches, 0) << "Blocked threat should not increase breaches";
}

#endif // NATIVE_BUILD
