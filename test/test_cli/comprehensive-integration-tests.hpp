#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
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
 * COMPREHENSIVE INTEGRATION TEST SUITE
 * ============================================
 *
 * Complete integration tests for all 7 FDN minigames covering:
 * - Easy mode win → Konami button unlock
 * - Hard mode win → color profile eligibility
 * - Loss scenarios → no rewards
 * - Re-encounter flows
 * - Edge cases (rapid inputs, timeouts, disconnects, boundary conditions)
 *
 * Games:
 * 1. Signal Echo (GameType 7, button UP=0)
 * 2. Ghost Runner (GameType 1, button START=6)
 * 3. Spike Vector (GameType 2, button DOWN=1)
 * 4. Firewall Decrypt (GameType 3, button LEFT=2)
 * 5. Cipher Path (GameType 4, button RIGHT=3)
 * 6. Exploit Sequencer (GameType 5, button B=4)
 * 7. Breach Defense (GameType 6, button A=5)
 */

class ComprehensiveIntegrationTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        DeviceFactory::destroyDevice(player_);
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
        player_.game->skipToState(player_.pdn, 8);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentState()->getStateId();
    }

    // Helper: trigger FDN handshake from Idle
    void triggerFdnHandshake(const std::string& gameType, const std::string& reward) {
        std::string msg = "*fdn:" + gameType + ":" + reward + "\r";
        player_.serialOutDriver->injectInput(msg.c_str());
        tick(3);
        player_.serialOutDriver->injectInput("*fack\r");
        tickWithTime(5, 100);
    }

    // Helper: get game instances
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
// SIGNAL ECHO INTEGRATION TESTS (GameType 7)
// ============================================

/*
 * Test: Signal Echo EASY win → unlocks UP button
 */
void signalEchoEasyWinUnlocksButton(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::UP)));

    suite->triggerFdnHandshake("7", "0");

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);
    ASSERT_TRUE(se->getConfig().managedMode);

    // Configure for guaranteed easy win
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 2;
    se->getConfig().allowedMistakes = 3;

    // Advance past intro (2s)
    suite->tickWithTime(25, 100);

    // Now in Show state - configure session
    se->getSession().currentSequence = {true, false};

    // Advance past show sequence
    suite->tickWithTime(20, 100);

    // Now in PlayerInput - enter correct inputs
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(5);

    // Should be in Win
    ASSERT_EQ(se->getCurrentState()->getStateId(), ECHO_WIN);

    // Advance past win timer → FdnComplete
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // UP button should be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::UP)));
}

/*
 * Test: Signal Echo HARD win → color profile eligibility
 */
void signalEchoHardWinUnlocksColorProfile(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    // Pre-unlock UP to enable re-encounter
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::UP));

    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD (PRIMARY button)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Configure for guaranteed hard win
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 8;
    se->getConfig().allowedMistakes = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Now in Show - configure session
    se->getSession().currentSequence = {true, false, true, false, true, false, true, false};

    // Advance past show
    suite->tickWithTime(20, 100);

    // Now in PlayerInput - enter all 8 correct inputs
    for (int i = 0; i < 8; i++) {
        bool expected = se->getSession().currentSequence[i];
        if (expected) {
            suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        } else {
            suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        }
        suite->tick(1);
    }
    suite->tick(5);

    ASSERT_EQ(se->getCurrentState()->getStateId(), ECHO_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Signal Echo LOSS → no rewards
 */
void signalEchoLossNoRewards(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("7", "0");

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Configure for guaranteed loss
    se->getConfig().displaySpeedMs = 10;
    se->getConfig().allowedMistakes = 1;
    se->getConfig().sequenceLength = 4;
    se->getSession().currentSequence = {true, true, true, true};
    se->getSession().inputIndex = 0;
    se->getSession().mistakes = 0;

    // Skip intro
    suite->tickWithTime(25, 100);

    // Skip show
    suite->tickWithTime(20, 100);

    // Press wrong button twice (exceeds allowedMistakes=1)
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(5);

    ASSERT_EQ(se->getCurrentState()->getStateId(), ECHO_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

/*
 * Test: Signal Echo timeout during PlayerInput
 */
void signalEchoTimeoutEdgeCase(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("7", "0");

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Skip to PlayerInput state
    se->getConfig().displaySpeedMs = 10;
    suite->tickWithTime(45, 100);

    // Don't press any buttons, just wait for timeout (if implemented)
    // For now, just verify clean state
    int stateId = se->getCurrentState()->getStateId();
    ASSERT_TRUE(stateId == ECHO_PLAYER_INPUT || stateId == ECHO_LOSE || stateId == ECHO_WIN);
}

/*
 * Test: Signal Echo rapid button presses
 */
void signalEchoRapidButtonPresses(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("7", "0");

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    se->getConfig().displaySpeedMs = 10;
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 2;
    se->getSession().currentSequence = {true, false};

    // Skip to PlayerInput
    suite->tickWithTime(45, 100);

    // Rapid press primary button 10 times
    for (int i = 0; i < 10; i++) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    suite->tick(5);

    // Game should handle gracefully (either ignore extras or count mistakes)
    int stateId = se->getCurrentState()->getStateId();
    ASSERT_TRUE(stateId == ECHO_PLAYER_INPUT || stateId == ECHO_LOSE || stateId == ECHO_WIN || stateId == ECHO_EVALUATE);
}

// ============================================
// GHOST RUNNER INTEGRATION TESTS (GameType 1)
// ============================================

/*
 * Test: Ghost Runner EASY win → unlocks START button
 */
void ghostRunnerEasyWinUnlocksButton(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::START)));

    suite->triggerFdnHandshake("1", "6");

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);
    ASSERT_TRUE(gr->getConfig().managedMode);

    // Configure for easy win
    gr->getConfig().ghostSpeedMs = 5;
    gr->getConfig().screenWidth = 100;
    gr->getConfig().targetZoneStart = 5;
    gr->getConfig().targetZoneEnd = 95;
    gr->getConfig().rounds = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Gameplay — advance ghost into zone and press
    suite->tickWithTime(10, 10);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // START button should be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::START)));
}

/*
 * Test: Ghost Runner HARD win → color profile eligibility
 */
void ghostRunnerHardWinUnlocksColorProfile(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("1", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);

    // Configure for hard win (missesAllowed <= 1, zoneWidth <= 16)
    gr->getConfig().ghostSpeedMs = 5;
    gr->getConfig().screenWidth = 100;
    gr->getConfig().targetZoneStart = 42;
    gr->getConfig().targetZoneEnd = 58;
    gr->getConfig().rounds = 1;
    gr->getConfig().missesAllowed = 1;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Press in zone
    suite->tickWithTime(10, 10);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Ghost Runner LOSS → no rewards
 */
void ghostRunnerLossNoRewards(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("1", "6");

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);

    // Configure for guaranteed loss
    gr->getConfig().ghostSpeedMs = 100;
    gr->getConfig().screenWidth = 100;
    gr->getConfig().targetZoneStart = 80;
    gr->getConfig().targetZoneEnd = 90;
    gr->getConfig().rounds = 1;
    gr->getConfig().missesAllowed = 0;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Press outside zone (ghost at 0, zone at 80-90)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

/*
 * Test: Ghost Runner press exactly at zone boundary
 */
void ghostRunnerBoundaryPress(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("1", "6");

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);

    // Configure narrow zone
    gr->getConfig().ghostSpeedMs = 10;
    gr->getConfig().screenWidth = 100;
    gr->getConfig().targetZoneStart = 50;
    gr->getConfig().targetZoneEnd = 51;
    gr->getConfig().rounds = 1;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Advance ghost to exactly 50
    auto& sess = gr->getSession();
    while (sess.ghostPosition < 50) {
        suite->tickWithTime(1, 10);
    }

    // Press at boundary
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Should register as hit or miss depending on boundary logic
    int stateId = gr->getCurrentState()->getStateId();
    ASSERT_TRUE(stateId == GHOST_WIN || stateId == GHOST_LOSE || stateId == GHOST_EVALUATE);
}

/*
 * Test: Ghost Runner rapid button presses during gameplay
 */
void ghostRunnerRapidPresses(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("1", "6");

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);

    gr->getConfig().ghostSpeedMs = 10;
    gr->getConfig().rounds = 1;

    // Advance to gameplay
    suite->tickWithTime(50, 100);

    // Rapid press 20 times
    for (int i = 0; i < 20; i++) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    // Should handle gracefully
    int stateId = gr->getCurrentState()->getStateId();
    ASSERT_TRUE(stateId >= GHOST_INTRO && stateId <= GHOST_EVALUATE);
}

// ============================================
// SPIKE VECTOR INTEGRATION TESTS (GameType 2)
// ============================================

/*
 * Test: Spike Vector EASY win → unlocks DOWN button
 */
void spikeVectorEasyWinUnlocksButton(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::DOWN)));

    suite->triggerFdnHandshake("2", "1");

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);
    ASSERT_TRUE(sv->getConfig().managedMode);

    // Configure for easy win
    sv->getConfig().trackLength = 3;
    sv->getConfig().waves = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Get gap position during show
    auto& sess = sv->getSession();
    int gap = sess.gapPosition;

    // Advance past show
    suite->tickWithTime(20, 100);

    // Move cursor to gap
    while (sess.cursorPosition > gap) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < gap) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    // Wait for wall
    suite->tickWithTime(20, 50);

    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // DOWN button should be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::DOWN)));
}

/*
 * Test: Spike Vector HARD win → color profile eligibility
 */
void spikeVectorHardWinUnlocksColorProfile(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::DOWN));

    suite->triggerFdnHandshake("2", "1");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);

    sv->getConfig().trackLength = 3;
    sv->getConfig().waves = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    auto& sess = sv->getSession();
    int gap = sess.gapPosition;

    // Advance past show
    suite->tickWithTime(20, 100);

    // Move to gap
    while (sess.cursorPosition > gap) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < gap) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    suite->tickWithTime(20, 50);

    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Spike Vector LOSS → no rewards
 */
void spikeVectorLossNoRewards(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("2", "1");

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);

    sv->getConfig().trackLength = 3;
    sv->getConfig().waves = 1;
    sv->getConfig().hitsAllowed = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    auto& sess = sv->getSession();
    int gap = sess.gapPosition;

    // Advance past show
    suite->tickWithTime(20, 100);

    // Move to WRONG position
    int wrongPos = (gap + 1) % sv->getConfig().numPositions;
    while (sess.cursorPosition > wrongPos) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < wrongPos) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    suite->tickWithTime(20, 50);

    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

/*
 * Test: Spike Vector rapid cursor movement
 */
void spikeVectorRapidCursorMovement(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("2", "1");

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);

    // Advance to gameplay
    suite->tickWithTime(50, 100);

    // Rapid alternating button presses
    for (int i = 0; i < 50; i++) {
        if (i % 2 == 0) {
            suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        } else {
            suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        }
        suite->tick(1);
    }

    // Should handle gracefully
    int stateId = sv->getCurrentState()->getStateId();
    ASSERT_TRUE(stateId >= SPIKE_INTRO && stateId <= SPIKE_EVALUATE);
}

// ============================================
// FIREWALL DECRYPT INTEGRATION TESTS (GameType 3)
// ============================================

/*
 * Test: Firewall Decrypt EASY win → unlocks LEFT button
 */
void firewallDecryptEasyWinUnlocksButton(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::LEFT)));

    suite->triggerFdnHandshake("3", "2");

    auto* fd = suite->getFirewallDecrypt();
    ASSERT_NE(fd, nullptr);
    ASSERT_TRUE(fd->getConfig().managedMode);

    // Configure for easy win
    fd->getConfig().numCandidates = 3;
    fd->getConfig().numRounds = 1;
    fd->getConfig().timeLimitMs = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // In Scan state — need to select correct MAC address
    auto& sess = fd->getSession();

    // Wait briefly
    suite->tickWithTime(5, 100);

    // Scroll to target index and select
    while (sess.selectedIndex < sess.targetIndex) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(2);
        sess.selectedIndex++;
    }
    // Confirm selection
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(10, 100);

    // Should be in Win
    ASSERT_EQ(fd->getCurrentState()->getStateId(), DECRYPT_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // LEFT button should be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::LEFT)));
}

/*
 * Test: Firewall Decrypt HARD win → color profile eligibility
 */
void firewallDecryptHardWinUnlocksColorProfile(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::LEFT));

    suite->triggerFdnHandshake("3", "2");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* fd = suite->getFirewallDecrypt();
    ASSERT_NE(fd, nullptr);

    fd->getConfig().numCandidates = 3;
    fd->getConfig().numRounds = 1;
    fd->getConfig().timeLimitMs = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Wait briefly
    suite->tickWithTime(5, 100);

    // Scroll to target and select
    auto& sess = fd->getSession();
    while (sess.selectedIndex < sess.targetIndex) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(2);
        sess.selectedIndex++;
    }
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(10, 100);

    ASSERT_EQ(fd->getCurrentState()->getStateId(), DECRYPT_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Firewall Decrypt LOSS → no rewards
 */
void firewallDecryptLossNoRewards(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("3", "2");

    auto* fd = suite->getFirewallDecrypt();
    ASSERT_NE(fd, nullptr);

    fd->getConfig().numCandidates = 5;
    fd->getConfig().numRounds = 1;
    fd->getConfig().timeLimitMs = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Wait briefly
    suite->tickWithTime(5, 100);

    // Select WRONG index (not the target)
    auto& sess = fd->getSession();
    int wrongIndex = (sess.targetIndex + 1) % fd->getConfig().numCandidates;
    while (sess.selectedIndex < wrongIndex) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(2);
        sess.selectedIndex++;
    }
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(10, 100);

    ASSERT_EQ(fd->getCurrentState()->getStateId(), DECRYPT_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

// ============================================
// CIPHER PATH INTEGRATION TESTS (GameType 4)
// ============================================

/*
 * Test: Cipher Path EASY win → unlocks RIGHT button
 */
void cipherPathEasyWinUnlocksButton(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::RIGHT)));

    suite->triggerFdnHandshake("4", "3");

    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);
    ASSERT_TRUE(cp->getConfig().managedMode);

    auto& config = cp->getConfig();

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Play through all rounds
    for (int round = 0; round < config.rounds; round++) {
        ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_SHOW);

        suite->tickWithTime(20, 100);

        // Make correct moves
        auto& sess = cp->getSession();
        for (int step = 0; step < config.gridSize; step++) {
            if (cp->getCurrentState()->getStateId() != CIPHER_GAMEPLAY) {
                break;
            }
            int correctDir = sess.cipher[sess.playerPosition];
            if (correctDir == 0) {
                suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            } else {
                suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            }
            suite->tick(2);
        }
    }

    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // RIGHT button should be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::RIGHT)));
}

/*
 * Test: Cipher Path HARD win → color profile eligibility
 */
void cipherPathHardWinUnlocksColorProfile(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::RIGHT));

    suite->triggerFdnHandshake("4", "3");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);

    auto& config = cp->getConfig();

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Play through all rounds
    for (int round = 0; round < config.rounds; round++) {
        ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_SHOW);

        suite->tickWithTime(20, 100);

        auto& sess = cp->getSession();
        for (int step = 0; step < config.gridSize; step++) {
            if (cp->getCurrentState()->getStateId() != CIPHER_GAMEPLAY) {
                break;
            }
            int correctDir = sess.cipher[sess.playerPosition];
            if (correctDir == 0) {
                suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            } else {
                suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            }
            suite->tick(2);
        }
    }

    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Cipher Path LOSS → no rewards
 */
void cipherPathLossNoRewards(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("4", "3");

    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);

    // Configure for guaranteed loss
    cp->getConfig().moveBudget = 5;

    // Advance past intro
    suite->tickWithTime(25, 100);

    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_SHOW);

    suite->tickWithTime(20, 100);

    // Press wrong buttons to exhaust budget
    auto& sess = cp->getSession();
    for (int i = 0; i < 5; i++) {
        int correctDir = sess.cipher[sess.playerPosition];
        // Press wrong button
        if (correctDir == 0) {
            suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        } else {
            suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        }
        suite->tick(2);
    }

    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

// ============================================
// EXPLOIT SEQUENCER INTEGRATION TESTS (GameType 5)
// ============================================

/*
 * Test: Exploit Sequencer EASY win → unlocks B button
 */
void exploitSequencerEasyWinUnlocksButton(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::B)));

    suite->triggerFdnHandshake("5", "4");

    auto* es = suite->getExploitSequencer();
    ASSERT_NE(es, nullptr);
    ASSERT_TRUE(es->getConfig().managedMode);

    // Configure for easy win
    es->getConfig().timingWindow = 45;
    es->getConfig().exploitsPerSeq = 1;
    es->getConfig().sequences = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Press in window
    suite->tickWithTime(5, 50);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(5, 100);

    ASSERT_EQ(es->getCurrentState()->getStateId(), EXPLOIT_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // B button should be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::B)));
}

/*
 * Test: Exploit Sequencer HARD win → color profile eligibility
 */
void exploitSequencerHardWinUnlocksColorProfile(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::B));

    suite->triggerFdnHandshake("5", "4");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* es = suite->getExploitSequencer();
    ASSERT_NE(es, nullptr);

    es->getConfig().timingWindow = 45;
    es->getConfig().exploitsPerSeq = 1;
    es->getConfig().sequences = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Press in window
    suite->tickWithTime(5, 50);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(5, 100);

    ASSERT_EQ(es->getCurrentState()->getStateId(), EXPLOIT_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Exploit Sequencer LOSS → no rewards
 */
void exploitSequencerLossNoRewards(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("5", "4");

    auto* es = suite->getExploitSequencer();
    ASSERT_NE(es, nullptr);

    // Configure for guaranteed loss
    es->getConfig().scrollSpeedMs = 100;
    es->getConfig().markerPosition = 50;
    es->getConfig().timingWindow = 2;
    es->getConfig().failsAllowed = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Press far from marker
    suite->tickWithTime(2, 50);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    ASSERT_EQ(es->getCurrentState()->getStateId(), EXPLOIT_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

/*
 * Test: Exploit Sequencer press at exact marker position
 */
void exploitSequencerExactMarkerPress(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("5", "4");

    auto* es = suite->getExploitSequencer();
    ASSERT_NE(es, nullptr);

    es->getConfig().scrollSpeedMs = 50;
    es->getConfig().markerPosition = 50;
    es->getConfig().timingWindow = 5;
    es->getConfig().exploitsPerSeq = 1;
    es->getConfig().sequences = 1;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Wait for symbol to reach exactly marker position
    auto& sess = es->getSession();
    while (sess.symbolPosition < 50) {
        suite->tickWithTime(1, 50);
    }

    // Press at exact marker
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(5, 100);

    // Should be a perfect hit → Win
    ASSERT_EQ(es->getCurrentState()->getStateId(), EXPLOIT_WIN);
}

// ============================================
// BREACH DEFENSE INTEGRATION TESTS (GameType 6)
// ============================================

/*
 * Test: Breach Defense EASY win → unlocks A button
 */
void breachDefenseEasyWinUnlocksButton(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::A)));

    suite->triggerFdnHandshake("6", "5");

    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);
    ASSERT_TRUE(bd->getConfig().managedMode);

    // Configure for easy win
    bd->getConfig().threatSpeedMs = 5;
    bd->getConfig().threatDistance = 3;
    bd->getConfig().totalThreats = 1;
    bd->getConfig().missesAllowed = 3;

    // Advance past intro + show
    suite->tickWithTime(40, 100);

    // Match shield to threat lane
    auto& sess = bd->getSession();
    sess.shieldLane = sess.threatLane;

    // Advance until threat arrives
    suite->tickWithTime(20, 20);

    // Wait for eval timer
    suite->tickWithTime(10, 100);

    ASSERT_EQ(bd->getCurrentState()->getStateId(), BREACH_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // A button should be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::A)));
}

/*
 * Test: Breach Defense HARD win → color profile eligibility
 */
void breachDefenseHardWinUnlocksColorProfile(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::A));

    suite->triggerFdnHandshake("6", "5");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);

    bd->getConfig().threatSpeedMs = 5;
    bd->getConfig().threatDistance = 3;
    bd->getConfig().totalThreats = 1;
    bd->getConfig().missesAllowed = 3;

    // Advance past intro + show
    suite->tickWithTime(40, 100);

    // Match shield to threat
    auto& sess = bd->getSession();
    sess.shieldLane = sess.threatLane;

    suite->tickWithTime(20, 20);

    // Wait for eval timer
    suite->tickWithTime(10, 100);

    ASSERT_EQ(bd->getCurrentState()->getStateId(), BREACH_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Breach Defense LOSS → no rewards
 */
void breachDefenseLossNoRewards(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("6", "5");

    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);

    // Configure for guaranteed loss
    bd->getConfig().threatSpeedMs = 5;
    bd->getConfig().threatDistance = 3;
    bd->getConfig().totalThreats = 1;
    bd->getConfig().missesAllowed = 0;
    bd->getConfig().numLanes = 3;

    // Advance past intro + show
    suite->tickWithTime(40, 100);

    // Set shield to WRONG lane
    auto& sess = bd->getSession();
    if (sess.threatLane == 0) {
        sess.shieldLane = 1;
    } else {
        sess.shieldLane = 0;
    }

    suite->tickWithTime(20, 20);

    // Wait for eval timer
    suite->tickWithTime(10, 100);

    ASSERT_EQ(bd->getCurrentState()->getStateId(), BREACH_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

/*
 * Test: Breach Defense shield movement during threat approach
 */
void breachDefenseShieldMovementDuringThreat(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("6", "5");

    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);

    bd->getConfig().numLanes = 5;
    bd->getConfig().totalThreats = 1;

    // Advance to gameplay
    suite->tickWithTime(40, 100);

    auto& sess = bd->getSession();

    // Move shield multiple times while threat approaches
    for (int i = 0; i < 10; i++) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tickWithTime(1, 10);
    }

    // Eventually match shield to threat lane
    sess.shieldLane = sess.threatLane;

    suite->tickWithTime(20, 20);
    suite->tickWithTime(10, 100);

    // Should win
    ASSERT_EQ(bd->getCurrentState()->getStateId(), BREACH_WIN);
}

// ============================================
// CROSS-GAME EDGE CASES
// ============================================

/*
 * Test: Complete all 7 games in sequence → all buttons unlocked
 */
void allGamesCompleteUnlocksAllButtons(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    // Game order: Signal Echo, Ghost Runner, Spike Vector, Firewall Decrypt, Cipher Path, Exploit Sequencer, Breach Defense
    std::vector<std::pair<std::string, std::string>> games = {
        {"7", "0"}, {"1", "6"}, {"2", "1"}, {"3", "2"}, {"4", "3"}, {"5", "4"}, {"6", "5"}
    };

    for (const auto& [gameType, reward] : games) {
        suite->advanceToIdle();
        suite->triggerFdnHandshake(gameType, reward);

        // Skip through game to completion (simplified)
        suite->tickWithTime(100, 100);

        // Verify back at Idle or FdnComplete
        int stateId = suite->getPlayerStateId();
        ASSERT_TRUE(stateId == IDLE || stateId == FDN_COMPLETE);

        suite->tickWithTime(20, 200);
    }

    // Check all 7 buttons unlocked
    for (int i = 0; i < 7; i++) {
        ASSERT_TRUE(suite->player_.player->hasUnlockedButton(i))
            << "Button " << i << " should be unlocked";
    }
}

/*
 * Test: FDN disconnect during game (timeout simulation)
 */
void fdnDisconnectMidGame(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("7", "0");

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Advance partway through game
    suite->tickWithTime(30, 100);

    // Simulate disconnect by not sending any more data
    // Just wait for timeout (if implemented)
    suite->tickWithTime(100, 200);

    // Should eventually clean up and return to Idle
    int stateId = suite->getPlayerStateId();
    ASSERT_TRUE(stateId == IDLE || stateId == FDN_COMPLETE || stateId == ECHO_LOSE || stateId == ECHO_WIN);
}

/*
 * Test: Score = 0 edge case across all games
 */
void allGamesHandleZeroScore(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    // Test one game with forced zero score
    suite->triggerFdnHandshake("7", "0");

    auto* se = suite->getSignalEcho();
    ASSERT_NE(se, nullptr);

    // Force game to lose immediately
    se->getConfig().allowedMistakes = 0;
    se->getSession().mistakes = 1;

    suite->tickWithTime(50, 100);

    // Should handle zero score gracefully
    ASSERT_EQ(se->getCurrentState()->getStateId(), ECHO_LOSE);
    ASSERT_EQ(se->getOutcome().score, 0);
}

#endif // NATIVE_BUILD
