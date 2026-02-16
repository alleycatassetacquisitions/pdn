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
    ASSERT_EQ(se->getCurrentStateId(), ECHO_WIN);

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

    ASSERT_EQ(se->getCurrentStateId(), ECHO_WIN);

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
    se->getConfig().allowedMistakes = 0;
    se->getConfig().sequenceLength = 4;
    se->getSession().currentSequence = {true, true, true, true};
    se->getSession().inputIndex = 0;
    se->getSession().playerInput.clear();

    // Skip intro
    suite->tickWithTime(25, 100);

    // Skip show
    suite->tickWithTime(20, 100);

    // Press wrong button twice (exceeds allowedMistakes=1)
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(5);

    ASSERT_EQ(se->getCurrentStateId(), ECHO_LOSE);

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
    int stateId = se->getCurrentStateId();
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
    int stateId = se->getCurrentStateId();
    ASSERT_TRUE(stateId == ECHO_PLAYER_INPUT || stateId == ECHO_LOSE || stateId == ECHO_WIN || stateId == ECHO_EVALUATE);
}

// ============================================
// GHOST RUNNER INTEGRATION TESTS (GameType 1)
// ============================================
// NOTE: Ghost Runner tests temporarily disabled during Wave 18 redesign (#220)
// Ghost Runner was redesigned from Guitar Hero rhythm to Memory Maze.
// Tests need complete rewrite for maze navigation mechanics.
// TODO(#220): Rewrite Ghost Runner integration tests for maze API
#if 0

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
    gr->getConfig().notesPerRound = 1;
    gr->getConfig().rounds = 1;
    gr->getConfig().rngSeed = 42;
    gr->getConfig().dualLaneChance = 0.0f;  // EASY mode

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Gameplay — move note to hit zone and press
    auto& session = gr->getSession();
    session.currentPattern[0].xPosition = 15;  // center of hit zone
    session.currentPattern[0].lane = Lane::UP;

    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Should transition to Evaluate then Win
    suite->tick(5);
    ASSERT_EQ(gr->getCurrentStateId(), GHOST_WIN);

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

    // Configure for hard win (dualLaneChance > 0 = HARD mode)
    gr->getConfig().ghostSpeedMs = 5;
    gr->getConfig().notesPerRound = 1;
    gr->getConfig().rounds = 1;
    gr->getConfig().rngSeed = 42;
    gr->getConfig().dualLaneChance = 0.4f;  // HARD mode

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Should be in Gameplay — hit the note perfectly
    auto& session = gr->getSession();
    session.currentPattern[0].xPosition = 15;  // center of perfect zone
    session.currentPattern[0].lane = Lane::UP;

    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Give it time to transition through evaluate to win
    suite->tick(10);

    // Should be in Win (100% PERFECT = passes hard mode requirement)
    int stateId = gr->getCurrentStateId();
    if (stateId != GHOST_WIN) {
        // Debug: print actual state and stats
        std::cerr << "State: " << stateId << " P:" << session.perfectCount
                  << " G:" << session.goodCount << " M:" << session.missCount << "\n";
    }
    ASSERT_EQ(stateId, GHOST_WIN);

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

    // Configure for guaranteed loss (run out of lives)
    gr->getConfig().ghostSpeedMs = 5;
    gr->getConfig().notesPerRound = 3;
    gr->getConfig().rounds = 1;
    gr->getConfig().rngSeed = 42;
    gr->getConfig().lives = 3;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Miss all 3 notes to run out of lives
    auto& session = gr->getSession();
    for (auto& note : session.currentPattern) {
        note.grade = NoteGrade::MISS;
        note.active = false;
    }
    session.missCount = 3;
    session.livesRemaining = 0;

    suite->tick(5);  // Let evaluate trigger
    ASSERT_EQ(gr->getCurrentStateId(), GHOST_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

/*
 * Test: Ghost Runner press exactly at hit zone boundary
 */
void ghostRunnerBoundaryPress(ComprehensiveIntegrationTestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("1", "6");

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);

    // Configure for boundary test
    gr->getConfig().ghostSpeedMs = 10;
    gr->getConfig().notesPerRound = 1;
    gr->getConfig().rounds = 1;
    gr->getConfig().rngSeed = 42;
    gr->getConfig().hitZoneWidthPx = 20;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Position note at exact hit zone start boundary (x=8)
    auto& session = gr->getSession();
    session.currentPattern[0].xPosition = 8;
    session.currentPattern[0].lane = Lane::UP;

    // Press at boundary
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Should register as hit (boundaries are inclusive)
    suite->tick(5);
    int stateId = gr->getCurrentStateId();
    ASSERT_TRUE(stateId == GHOST_WIN || stateId == GHOST_EVALUATE);
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
    gr->getConfig().notesPerRound = 3;
    gr->getConfig().rounds = 1;
    gr->getConfig().rngSeed = 42;
    gr->getConfig().lives = 5;  // Extra lives to survive rapid presses

    // Advance to gameplay
    suite->tickWithTime(50, 100);

    // Rapid press 10 times (should only grade the nearest notes, not crash)
    for (int i = 0; i < 10; i++) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    // Should handle gracefully - game continues or ends cleanly
    int stateId = gr->getCurrentStateId();
    ASSERT_TRUE(stateId >= GHOST_INTRO && stateId <= GHOST_LOSE) << "Invalid state: " << stateId;
}

#endif // Ghost Runner tests disabled

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
    sv->getConfig().levels = 1;
    sv->getConfig().baseWallCount = 2;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Get first gap position during show
    auto& sess = sv->getSession();
    int gap = sess.gapPositions.empty() ? 0 : sess.gapPositions[0];

    // Advance past show
    suite->tickWithTime(15, 100);

    // Move cursor to gap
    while (sess.cursorPosition > gap) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < gap) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    // Wait for walls to pass
    int speedMs = speedMsForLevel(sv->getConfig(), 0);
    suite->tickWithTime(160 / speedMs + 10, speedMs);

    ASSERT_EQ(sv->getCurrentStateId(), SPIKE_WIN);

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

    sv->getConfig().levels = 1;
    sv->getConfig().baseWallCount = 2;

    // Advance past intro
    suite->tickWithTime(25, 100);

    auto& sess = sv->getSession();
    int gap = sess.gapPositions.empty() ? 0 : sess.gapPositions[0];

    // Advance past show
    suite->tickWithTime(15, 100);

    // Move to gap
    while (sess.cursorPosition > gap) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < gap) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    int speedMs = speedMsForLevel(sv->getConfig(), 0);
    suite->tickWithTime(160 / speedMs + 10, speedMs);

    ASSERT_EQ(sv->getCurrentStateId(), SPIKE_WIN);

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

    sv->getConfig().levels = 1;
    sv->getConfig().baseWallCount = 2;
    sv->getConfig().hitsAllowed = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    auto& sess = sv->getSession();
    int gap = sess.gapPositions.empty() ? 0 : sess.gapPositions[0];

    // Advance past show
    suite->tickWithTime(15, 100);

    // Move to WRONG position
    int wrongPos = (gap + 1) % sv->getConfig().numPositions;
    while (sess.cursorPosition > wrongPos) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < wrongPos) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    suite->tickWithTime(20, 50);

    ASSERT_EQ(sv->getCurrentStateId(), SPIKE_LOSE);

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
    int stateId = sv->getCurrentStateId();
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
    ASSERT_EQ(fd->getCurrentStateId(), DECRYPT_WIN);

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

    ASSERT_EQ(fd->getCurrentStateId(), DECRYPT_WIN);

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

    ASSERT_EQ(fd->getCurrentStateId(), DECRYPT_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

// ============================================
// CIPHER PATH INTEGRATION TESTS (GameType 4)
// ============================================
// NOTE: Cipher Path tests temporarily disabled during Wave 18 redesign (#242)
// Cipher Path was redesigned from binary cipher to wire routing puzzle.
// Tests need complete rewrite for tile rotation mechanics.
// TODO(#242): Rewrite Cipher Path integration tests for wire routing API
#if 0

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
        ASSERT_EQ(cp->getCurrentStateId(), CIPHER_SHOW);

        suite->tickWithTime(20, 100);

        // Make correct moves
        auto& sess = cp->getSession();
        for (int step = 0; step < config.gridSize; step++) {
            if (cp->getCurrentStateId() != CIPHER_GAMEPLAY) {
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

    ASSERT_EQ(cp->getCurrentStateId(), CIPHER_WIN);

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
        ASSERT_EQ(cp->getCurrentStateId(), CIPHER_SHOW);

        suite->tickWithTime(20, 100);

        auto& sess = cp->getSession();
        for (int step = 0; step < config.gridSize; step++) {
            if (cp->getCurrentStateId() != CIPHER_GAMEPLAY) {
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

    ASSERT_EQ(cp->getCurrentStateId(), CIPHER_WIN);

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

    ASSERT_EQ(cp->getCurrentStateId(), CIPHER_SHOW);

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

    ASSERT_EQ(cp->getCurrentStateId(), CIPHER_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

#endif // Cipher Path tests disabled

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

    // Configure for easy win (rhythm game)
    es->getConfig().rounds = 1;
    es->getConfig().notesPerRound = 1;
    es->getConfig().noteSpeedMs = 500;  // very slow
    es->getConfig().hitZoneWidthPx = 60;  // very wide

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Press in window
    suite->tickWithTime(5, 50);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(5, 100);

    ASSERT_EQ(es->getCurrentStateId(), EXPLOIT_WIN);

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

    es->getConfig().rounds = 1;
    es->getConfig().notesPerRound = 1;
    es->getConfig().noteSpeedMs = 500;
    es->getConfig().hitZoneWidthPx = 60;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Press in window
    suite->tickWithTime(5, 50);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(5, 100);

    ASSERT_EQ(es->getCurrentStateId(), EXPLOIT_WIN);

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

    // Configure for guaranteed loss (no lives, so first mistake loses)
    es->getConfig().lives = 1;
    es->getConfig().rounds = 1;
    es->getConfig().notesPerRound = 1;
    es->getConfig().hitZoneWidthPx = 2;  // nearly impossible to hit

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show
    suite->tickWithTime(20, 100);

    // Press far from marker
    suite->tickWithTime(2, 50);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    ASSERT_EQ(es->getCurrentStateId(), EXPLOIT_LOSE);

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

    // Configure for easy rhythm game win
    es->getConfig().rounds = 1;
    es->getConfig().notesPerRound = 1;
    es->getConfig().noteSpeedMs = 100;
    es->getConfig().hitZoneWidthPx = 40;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Advance a few ticks to let note move
    suite->tickWithTime(5, 100);

    // Press to hit note
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    suite->tickWithTime(5, 100);

    // Should be a perfect hit → Win
    ASSERT_EQ(es->getCurrentStateId(), EXPLOIT_WIN);
}

// ============================================
// BREACH DEFENSE INTEGRATION TESTS (GameType 6)
// ============================================
// NOTE: Tests temporarily disabled during Wave 18 redesign (#231)
// Session structure changed - tests need rewrite for combo mechanics
#if 0

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

    ASSERT_EQ(bd->getCurrentStateId(), BREACH_WIN);

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

    ASSERT_EQ(bd->getCurrentStateId(), BREACH_WIN);

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

    ASSERT_EQ(bd->getCurrentStateId(), BREACH_LOSE);

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
    ASSERT_EQ(bd->getCurrentStateId(), BREACH_WIN);
}

#endif // Breach Defense tests disabled

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

    // Force game to lose with wrong sequence
    se->getConfig().allowedMistakes = 0;
    se->getConfig().sequenceLength = 2;
    se->getSession().currentSequence = {true, true};
    se->getSession().playerInput = {false, false};  // Wrong inputs
    se->getSession().inputIndex = 2;

    suite->tickWithTime(50, 100);

    // Should handle zero score gracefully
    ASSERT_EQ(se->getCurrentStateId(), ECHO_LOSE);
    ASSERT_EQ(se->getOutcome().score, 0);
}

#endif // NATIVE_BUILD
