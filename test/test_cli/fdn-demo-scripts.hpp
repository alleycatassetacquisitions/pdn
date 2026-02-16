#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/minigame.hpp"
#include "game/color-profiles.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

/*
 * ============================================
 * FDN DEMO WALKTHROUGH SCRIPT TEST SUITE
 * ============================================
 *
 * Automated walkthrough scripts that demonstrate full FDN gameplay flow
 * from cable connect through minigame completion. These scripts serve as:
 * - Integration test verification
 * - Demo/playtest automation
 * - Documentation of expected game flow
 *
 * Each walkthrough covers:
 * 1. Cable connect
 * 2. FDN detection and handshake
 * 3. Minigame start (intro)
 * 4. Gameplay execution
 * 5. Win/loss evaluation
 * 6. Rewards (Konami buttons, color profiles)
 * 7. Clean disconnect
 *
 * Coverage:
 * - Happy path: 3+ minigame types (easy + hard)
 * - Error cases: disconnect mid-game, timeout, invalid inputs
 */

class FdnDemoScriptTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(player_);

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
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
        // stateMap index 6 = Idle (after removing AllegiancePickerState)
        player_.game->skipToState(player_.pdn, 6);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentStateId();
    }

    // Helper: trigger FDN handshake from Idle
    // gameType: "1" = Ghost Runner, "2" = Spike Vector, "7" = Signal Echo, etc.
    // reward: "0" = easy (Konami button), "6" = hard (color profile eligible)
    void triggerFdnHandshake(const std::string& gameType, const std::string& reward) {
        std::string msg = "*fdn:" + gameType + ":" + reward + "\r";
        player_.serialOutDriver->injectInput(msg.c_str());
        tick(3);
        player_.serialOutDriver->injectInput("*fack\r");
        tickWithTime(5, 100);
    }

    // Helper: verify player is in expected state
    void assertPlayerState(int expectedStateId, const std::string& stateName) {
        int actualState = getPlayerStateId();
        ASSERT_EQ(actualState, expectedStateId)
            << "Expected " << stateName << " (ID: " << expectedStateId
            << "), got state ID: " << actualState;
    }

    // Helper: verify Konami button unlock
    void assertButtonUnlocked(KonamiButton button, bool shouldBeUnlocked) {
        bool isUnlocked = player_.player->hasUnlockedButton(static_cast<uint8_t>(button));
        if (shouldBeUnlocked) {
            ASSERT_TRUE(isUnlocked) << "Button should be unlocked";
        } else {
            ASSERT_FALSE(isUnlocked) << "Button should NOT be unlocked";
        }
    }

    // Helper: verify color profile eligibility
    void assertColorProfileEligibility(GameType gameType, bool shouldBeEligible) {
        bool isEligible = player_.player->hasColorProfileEligibility(static_cast<int>(gameType));
        if (shouldBeEligible) {
            ASSERT_TRUE(isEligible) << "Should be eligible for color profile";
        } else {
            ASSERT_FALSE(isEligible) << "Should NOT be eligible for color profile";
        }
    }

    // Get game instances
    SignalEcho* getSignalEcho() {
        return static_cast<SignalEcho*>(
            player_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    }

    GhostRunner* getGhostRunner() {
        return static_cast<GhostRunner*>(
            player_.pdn->getApp(StateId(GHOST_RUNNER_APP_ID)));
    }

    SpikeVector* getSpikeVector() {
        return static_cast<SpikeVector*>(
            player_.pdn->getApp(StateId(SPIKE_VECTOR_APP_ID)));
    }

    FirewallDecrypt* getFirewallDecrypt() {
        return static_cast<FirewallDecrypt*>(
            player_.pdn->getApp(StateId(FIREWALL_DECRYPT_APP_ID)));
    }

    CipherPath* getCipherPath() {
        return static_cast<CipherPath*>(
            player_.pdn->getApp(StateId(CIPHER_PATH_APP_ID)));
    }

    ExploitSequencer* getExploitSequencer() {
        return static_cast<ExploitSequencer*>(
            player_.pdn->getApp(StateId(EXPLOIT_SEQUENCER_APP_ID)));
    }

    BreachDefense* getBreachDefense() {
        return static_cast<BreachDefense*>(
            player_.pdn->getApp(StateId(BREACH_DEFENSE_APP_ID)));
    }

    DeviceInstance player_;
};

// ============================================
// SIGNAL ECHO WALKTHROUGH (GameType 7)
// ============================================

/*
 * Demo Script: Signal Echo Easy Mode - Complete Walkthrough
 *
 * Flow:
 * 1. Start in Idle
 * 2. Detect FDN broadcast (Signal Echo, easy reward)
 * 3. Complete handshake
 * 4. Enter Signal Echo minigame
 * 5. Play through intro
 * 6. Complete gameplay successfully
 * 7. Win state → unlock UP button
 * 8. Return to Idle
 */
void signalEchoEasyCompleteWalkthrough(FdnDemoScriptTestSuite* suite) {
    // STEP 1: Start in Idle
    suite->advanceToIdle();
    suite->assertPlayerState(IDLE, "Idle");
    suite->assertButtonUnlocked(KonamiButton::UP, false);

    // STEP 2-3: Cable connect → FDN detection → handshake
    suite->triggerFdnHandshake("7", "0");  // Signal Echo, easy mode

    // Should transition through FdnDetected → SignalEcho app
    suite->tick(5);

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr) << "Signal Echo app should be loaded";
    ASSERT_TRUE(se->getConfig().managedMode) << "Should be in managed mode";

    // STEP 4-5: Configure for guaranteed easy win
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 2;
    se->getConfig().allowedMistakes = 3;

    // Advance past intro (2s)
    suite->tickWithTime(25, 100);

    // STEP 6: Gameplay - show sequence
    ASSERT_EQ(se->getCurrentStateId(), ECHO_SHOW_SEQUENCE);
    se->getSession().currentSequence = {true, false};  // PRIMARY, SECONDARY

    // Wait for sequence display
    suite->tickWithTime(20, 100);

    // Player input phase
    ASSERT_EQ(se->getCurrentStateId(), ECHO_PLAYER_INPUT);

    // Enter correct sequence: PRIMARY (true), SECONDARY (false)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(5);

    // STEP 7: Win state
    ASSERT_EQ(se->getCurrentStateId(), ECHO_WIN);

    // Advance past win timer → FdnComplete
    suite->tickWithTime(35, 100);
    suite->assertPlayerState(FDN_COMPLETE, "FdnComplete");

    // STEP 8: Verify UP button unlocked
    suite->assertButtonUnlocked(KonamiButton::UP, true);

    // Return to Idle after completion timer
    suite->tickWithTime(60, 100);
    suite->assertPlayerState(IDLE, "Idle");
}

/*
 * Demo Script: Signal Echo Hard Mode - Complete Walkthrough
 *
 * Requires pre-unlocked UP button for re-encounter flow.
 * Tests hard mode difficulty selection and color profile reward.
 */
void signalEchoHardCompleteWalkthrough(FdnDemoScriptTestSuite* suite) {
    // Pre-unlock UP button to enable re-encounter
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::UP));
    suite->assertButtonUnlocked(KonamiButton::UP, true);
    suite->assertColorProfileEligibility(GameType::SIGNAL_ECHO, false);

    // Trigger FDN with easy reward (triggers re-encounter prompt)
    suite->triggerFdnHandshake("7", "0");
    suite->assertPlayerState(FDN_REENCOUNTER, "FdnReencounter");

    // Choose HARD mode (PRIMARY button)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 100);

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);
    ASSERT_TRUE(se->getConfig().managedMode);

    // Configure for guaranteed hard win
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 3;
    se->getConfig().allowedMistakes = 0;  // Hard mode - no mistakes

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Gameplay
    ASSERT_EQ(se->getCurrentStateId(), ECHO_SHOW_SEQUENCE);
    se->getSession().currentSequence = {true, true, false};
    suite->tickWithTime(20, 100);

    // Player input - correct sequence
    ASSERT_EQ(se->getCurrentStateId(), ECHO_PLAYER_INPUT);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(5);

    // Win
    ASSERT_EQ(se->getCurrentStateId(), ECHO_WIN);
    suite->tickWithTime(35, 100);

    // Should route to BoonAwarded (color profile reward)
    suite->assertPlayerState(BOON_AWARDED, "BoonAwarded");

    // Verify color profile eligibility
    suite->assertColorProfileEligibility(GameType::SIGNAL_ECHO, true);

    // Advance past boon awarded → color profile prompt
    suite->tickWithTime(40, 100);
    suite->assertPlayerState(COLOR_PROFILE_PROMPT, "ColorProfilePrompt");

    // Accept color profile (PRIMARY)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 100);

    // Return to Idle
    suite->assertPlayerState(IDLE, "Idle");
}

// ============================================
// GHOST RUNNER WALKTHROUGH (GameType 1)
// ============================================
// NOTE: Tests disabled during Wave 18 redesign (#220) — Guitar Hero → Memory Maze
// TODO(#220): Rewrite Ghost Runner demo walkthrough for maze API
#if 0

/*
 * Demo Script: Ghost Runner Easy Mode - Complete Walkthrough
 *
 * Wave 18 redesign: Memory maze navigation game.
 * Player sees maze layout briefly, then navigates invisible maze from memory.
 * Ghost Runner button: START (6)
 */
void ghostRunnerEasyCompleteWalkthrough(FdnDemoScriptTestSuite* suite) {
    suite->advanceToIdle();
    suite->assertPlayerState(IDLE, "Idle");
    suite->assertButtonUnlocked(KonamiButton::START, false);

    // Trigger Ghost Runner FDN
    suite->triggerFdnHandshake("1", "0");  // Ghost Runner, easy mode
    suite->tick(5);

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr) << "Ghost Runner app should be loaded";
    ASSERT_TRUE(gr->getConfig().managedMode);

    // Configure for guaranteed easy win
    gr->getConfig().rounds = 2;
    gr->getConfig().cols = 3;
    gr->getConfig().rows = 2;
    gr->getConfig().lives = 5;
    gr->getConfig().previewMazeMs = 2000;
    gr->getConfig().previewTraceMs = 2000;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Gameplay states: GHOST_PREVIEW_MAZE -> GHOST_PREVIEW_TRACE -> GHOST_GAMEPLAY
    int stateId = gr->getCurrentStateId();

    // Wait through preview phases (maze + solution trace)
    suite->tickWithTime(50, 100);  // ~5 seconds for both previews

    // Now in GHOST_GAMEPLAY - navigate maze from memory
    // PRIMARY = cycle direction, SECONDARY = move forward
    // For test, manually set cursor to end position
    auto& sess = gr->getSession();
    sess.cursorRow = gr->getConfig().exitRow;
    sess.cursorCol = gr->getConfig().exitCol;
    sess.currentRound++;

    suite->tick(5);

    // Complete second round (simplified for test)
    sess.cursorRow = gr->getConfig().exitRow;
    sess.cursorCol = gr->getConfig().exitCol;

    suite->tick(5);

    // Should be in Win state
    ASSERT_EQ(gr->getCurrentStateId(), GHOST_WIN);

    // Advance to FdnComplete
    suite->tickWithTime(35, 100);
    suite->assertPlayerState(FDN_COMPLETE, "FdnComplete");

    // Verify START button unlocked
    suite->assertButtonUnlocked(KonamiButton::START, true);

    // Return to Idle
    suite->tickWithTime(60, 100);
    suite->assertPlayerState(IDLE, "Idle");
}

#endif // Ghost Runner demo tests disabled

// ============================================
// SPIKE VECTOR WALKTHROUGH (GameType 2)
// ============================================

/*
 * Demo Script: Spike Vector Easy Mode - Complete Walkthrough
 *
 * Spike Vector button: DOWN (1)
 */
void spikeVectorEasyCompleteWalkthrough(FdnDemoScriptTestSuite* suite) {
    suite->advanceToIdle();
    suite->assertPlayerState(IDLE, "Idle");
    suite->assertButtonUnlocked(KonamiButton::DOWN, false);

    // Trigger Spike Vector FDN
    suite->triggerFdnHandshake("2", "0");  // Spike Vector, easy mode
    suite->tick(5);

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr) << "Spike Vector app should be loaded";
    ASSERT_TRUE(sv->getConfig().managedMode);

    // Configure for guaranteed easy win
    sv->getConfig().levels = 2;
    sv->getConfig().baseWallCount = 2;
    sv->getConfig().hitsAllowed = 5;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Simulate 2 levels (configured)
    for (int level = 0; level < 2; level++) {
        // Get first gap position during show phase
        auto& sess = sv->getSession();
        int gap = sess.gapPositions.empty() ? 0 : sess.gapPositions[0];

        // Advance past show
        suite->tickWithTime(15, 100);

        // Move cursor to gap position
        while (sess.cursorPosition > gap) {
            suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            suite->tick(1);
        }
        while (sess.cursorPosition < gap) {
            suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            suite->tick(1);
        }

        // Wait for walls to pass
        int speedMs = speedMsForLevel(sv->getConfig(), level);
        suite->tickWithTime(160 / speedMs + 10, speedMs);
    }

    // Should be in Win state
    ASSERT_EQ(sv->getCurrentStateId(), SPIKE_WIN);

    // Advance to FdnComplete
    suite->tickWithTime(35, 100);
    suite->assertPlayerState(FDN_COMPLETE, "FdnComplete");

    // Verify DOWN button unlocked
    suite->assertButtonUnlocked(KonamiButton::DOWN, true);

    // Return to Idle
    suite->tickWithTime(60, 100);
    suite->assertPlayerState(IDLE, "Idle");
}

// ============================================
// ERROR CASE WALKTHROUGHS
// ============================================

/*
 * Demo Script: Disconnect During Handshake
 *
 * Tests that player handles FDN disconnect gracefully during handshake.
 */
void fdnDisconnectDuringHandshake(FdnDemoScriptTestSuite* suite) {
    suite->advanceToIdle();
    suite->assertPlayerState(IDLE, "Idle");

    // Trigger FDN broadcast
    suite->player_.serialOutDriver->injectInput("*fdn:7:0\r");
    suite->tick(3);

    // Should be in FdnDetected
    suite->assertPlayerState(FDN_DETECTED, "FdnDetected");

    // Simulate disconnect by NOT sending fack and timing out
    // FdnDetected has a 5-second timeout
    suite->tickWithTime(60, 100);  // 6 seconds

    // Should return to Idle after timeout
    suite->assertPlayerState(IDLE, "Idle");

    // No buttons should be unlocked
    suite->assertButtonUnlocked(KonamiButton::UP, false);
}

/*
 * Demo Script: Loss Scenario - Signal Echo
 *
 * Tests that losing a game does NOT unlock rewards.
 */
void signalEchoLossNoRewards(FdnDemoScriptTestSuite* suite) {
    suite->advanceToIdle();
    suite->assertButtonUnlocked(KonamiButton::UP, false);

    // Trigger Signal Echo FDN
    suite->triggerFdnHandshake("7", "0");
    suite->tick(5);

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Configure for guaranteed loss (impossible sequence)
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 5;
    se->getConfig().allowedMistakes = 0;  // No mistakes allowed

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Gameplay - intentionally make mistake
    ASSERT_EQ(se->getCurrentStateId(), ECHO_SHOW_SEQUENCE);
    se->getSession().currentSequence = {true, true, false, true, false};
    suite->tickWithTime(20, 100);

    // Player input - wrong first input
    ASSERT_EQ(se->getCurrentStateId(), ECHO_PLAYER_INPUT);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Wrong!
    suite->tick(5);

    // Should be in Loss state
    ASSERT_EQ(se->getCurrentStateId(), ECHO_LOSE);

    // Advance to FdnComplete
    suite->tickWithTime(35, 100);
    suite->assertPlayerState(FDN_COMPLETE, "FdnComplete");

    // Verify UP button NOT unlocked
    suite->assertButtonUnlocked(KonamiButton::UP, false);

    // Return to Idle
    suite->tickWithTime(60, 100);
    suite->assertPlayerState(IDLE, "Idle");
}

/*
 * Demo Script: Multiple Errors Lead to Loss
 *
 * Tests that accumulating too many mistakes triggers loss condition.
 */
void signalEchoMultipleErrorsLoss(FdnDemoScriptTestSuite* suite) {
    suite->advanceToIdle();
    suite->assertButtonUnlocked(KonamiButton::UP, false);

    suite->triggerFdnHandshake("7", "0");
    suite->tick(5);

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Configure game - no mistakes allowed
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 3;
    se->getConfig().allowedMistakes = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Show sequence
    ASSERT_EQ(se->getCurrentStateId(), ECHO_SHOW_SEQUENCE);
    se->getSession().currentSequence = {true, false, true};
    suite->tickWithTime(20, 100);

    // Player input phase - make one wrong input
    ASSERT_EQ(se->getCurrentStateId(), ECHO_PLAYER_INPUT);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Wrong! Should be PRIMARY
    suite->tick(5);

    // Should be in Loss state immediately (0 mistakes allowed)
    ASSERT_EQ(se->getCurrentStateId(), ECHO_LOSE);

    // Verify no button unlock
    suite->assertButtonUnlocked(KonamiButton::UP, false);
}

/*
 * Demo Script: Rapid Button Spam During Gameplay
 *
 * Tests that game handles rapid button presses without crashing.
 */
void signalEchoRapidButtonSpam(FdnDemoScriptTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("7", "0");
    suite->tick(5);

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Configure game
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 2;
    se->getConfig().allowedMistakes = 10;  // Allow many mistakes

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Show sequence
    ASSERT_EQ(se->getCurrentStateId(), ECHO_SHOW_SEQUENCE);
    se->getSession().currentSequence = {true, false};
    suite->tickWithTime(20, 100);

    // Player input - spam both buttons rapidly
    ASSERT_EQ(se->getCurrentStateId(), ECHO_PLAYER_INPUT);
    for (int i = 0; i < 20; i++) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    // Game should still be running or in loss state, not crashed
    int stateId = se->getCurrentStateId();
    ASSERT_TRUE(stateId == ECHO_PLAYER_INPUT || stateId == ECHO_LOSE || stateId == ECHO_WIN);
}

#endif  // NATIVE_BUILD
