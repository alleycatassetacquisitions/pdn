#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/minigame.hpp"
#include "game/color-profiles.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// E2E GAME SUITE TEST FIXTURE
//
// End-to-end tests for the 5 new minigames:
// Ghost Runner, Spike Vector, Cipher Path,
// Exploit Sequencer, Breach Defense.
//
// Tests cover:
// - EASY win → Konami button unlock
// - HARD win → color profile eligibility
// - Loss scenarios → no rewards
// - Re-encounter flow for each game
// ============================================

class E2EGameSuiteTestSuite : public testing::Test {
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
        player_.game->skipToState(player_.pdn, 7);
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
    GhostRunner* getGhostRunner() {
        return static_cast<GhostRunner*>(
            player_.pdn->getApp(StateId(GHOST_RUNNER_APP_ID)));
    }

    SpikeVector* getSpikeVector() {
        return static_cast<SpikeVector*>(
            player_.pdn->getApp(StateId(SPIKE_VECTOR_APP_ID)));
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
// GHOST RUNNER E2E TESTS
// ============================================

/*
 * Test: Ghost Runner EASY win → unlocks START button.
 */
void e2eGhostRunnerEasyWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasKonamiBoon());

    // Trigger FDN for Ghost Runner (GameType 1, KonamiButton START=6)
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

    // Advance past intro timer (2s)
    suite->tickWithTime(25, 100);

    // Advance past show timer (1.5s)
    suite->tickWithTime(20, 100);

    // Should be in Gameplay — advance ghost into zone and press
    suite->tickWithTime(10, 10);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Should be in Win
    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_WIN);
    ASSERT_EQ(gr->getOutcome().result, MiniGameResult::WON);

    // Advance past win timer (3s) → returns to FdnComplete
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // START button (bit 6) should be unlocked
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 6));
}

/*
 * Test: Ghost Runner HARD win → color profile eligibility.
 * NOTE: This test is currently failing - FdnComplete is not transitioning to COLOR_PROFILE_PROMPT.
 * Need to investigate why pendingProfileGame is not being set correctly.
 */
void e2eGhostRunnerHardWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    // Pre-unlock START button to enable re-encounter
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("1", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD (PRIMARY button)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);

    // Configure for easy win that qualifies as HARD (missesAllowed <= 1, zoneWidth <= 16)
    gr->getConfig().ghostSpeedMs = 5;
    gr->getConfig().screenWidth = 100;
    gr->getConfig().targetZoneStart = 42;
    gr->getConfig().targetZoneEnd = 58;  // zoneWidth = 16
    gr->getConfig().rounds = 1;
    gr->getConfig().missesAllowed = 1;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Gameplay — press in zone
    suite->tickWithTime(10, 10);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    suite->tickWithTime(5, 10);  // FdnReencounter → FdnComplete transition

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);  // FdnComplete display timer
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Ghost Runner loss → no rewards.
 * NOTE: This test is currently failing - game ends in WIN instead of LOSE.
 * Need to investigate the loss condition logic more carefully.
 */
void e2eGhostRunnerLoss(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("1", "6");

    auto* gr = suite->getGhostRunner();
    ASSERT_NE(gr, nullptr);

    // Configure for guaranteed loss — miss allowed is 0, press outside zone
    gr->getConfig().ghostSpeedMs = 100;
    gr->getConfig().screenWidth = 100;
    gr->getConfig().targetZoneStart = 80;
    gr->getConfig().targetZoneEnd = 90;
    gr->getConfig().rounds = 1;
    gr->getConfig().missesAllowed = 0;

    // Advance past intro + show
    suite->tickWithTime(50, 100);

    // Press immediately while ghost is at position 0 (far from zone 80-90)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(5, 100);

    // Should be in Lose
    ASSERT_EQ(gr->getCurrentState()->getStateId(), GHOST_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

// ============================================
// SPIKE VECTOR E2E TESTS
// ============================================

/*
 * Test: Spike Vector EASY win → unlocks DOWN button.
 */
void e2eSpikeVectorEasyWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN for Spike Vector (GameType 2, KonamiButton DOWN=1)
    suite->triggerFdnHandshake("2", "1");

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);
    ASSERT_TRUE(sv->getConfig().managedMode);

    // Configure for easy win
    sv->getConfig().trackLength = 3;
    sv->getConfig().waves = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // In Show — get gap position
    auto& sess = sv->getSession();
    int gap = sess.gapPosition;

    // Advance past show timer
    suite->tickWithTime(20, 100);

    // In Gameplay — move cursor to gap
    while (sess.cursorPosition > gap) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < gap) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    // Advance until wall arrives
    suite->tickWithTime(20, 50);

    // Should be in Win
    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // DOWN button (bit 1) should be unlocked
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 1));
}

/*
 * Test: Spike Vector HARD win → color profile eligibility.
 */
void e2eSpikeVectorHardWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::DOWN));

    suite->triggerFdnHandshake("2", "1");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);

    // Configure for easy win
    sv->getConfig().trackLength = 3;
    sv->getConfig().waves = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // In Show — get gap
    auto& sess = sv->getSession();
    int gap = sess.gapPosition;

    // Advance past show timer
    suite->tickWithTime(20, 100);

    // Move cursor to gap
    while (sess.cursorPosition > gap) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < gap) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    // Advance until wall arrives
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
 * Test: Spike Vector loss → no rewards.
 */
void e2eSpikeVectorLoss(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("2", "1");

    auto* sv = suite->getSpikeVector();
    ASSERT_NE(sv, nullptr);

    // Configure for guaranteed loss — wrong cursor position
    sv->getConfig().trackLength = 3;
    sv->getConfig().waves = 1;
    sv->getConfig().hitsAllowed = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // In Show — get gap
    auto& sess = sv->getSession();
    int gap = sess.gapPosition;

    // Advance past show timer
    suite->tickWithTime(20, 100);

    // Move cursor to WRONG position
    int wrongPos = (gap + 1) % sv->getConfig().numPositions;
    while (sess.cursorPosition > wrongPos) {
        suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    while (sess.cursorPosition < wrongPos) {
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    // Advance until wall arrives
    suite->tickWithTime(20, 50);

    ASSERT_EQ(sv->getCurrentState()->getStateId(), SPIKE_LOSE);

    // Advance past lose timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
}

// ============================================
// CIPHER PATH E2E TESTS
// ============================================

/*
 * Test: Cipher Path EASY win → unlocks RIGHT button.
 */
void e2eCipherPathEasyWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN for Cipher Path (GameType 4, KonamiButton RIGHT=3)
    suite->triggerFdnHandshake("4", "3");

    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);
    ASSERT_TRUE(cp->getConfig().managedMode);

    auto& config = cp->getConfig();

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Play through all rounds
    for (int round = 0; round < config.rounds; round++) {
        // In Show
        ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_SHOW);

        // Advance past show timer
        suite->tickWithTime(20, 100);

        // In Gameplay — make correct moves until exit
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

    // Should be in Win
    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // RIGHT button (bit 3) should be unlocked
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 3));
}

/*
 * Test: Cipher Path HARD win → color profile eligibility.
 */
void e2eCipherPathHardWin(E2EGameSuiteTestSuite* suite) {
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

        // Advance past show timer
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
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Cipher Path loss → no rewards.
 */
void e2eCipherPathLoss(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("4", "3");

    auto* cp = suite->getCipherPath();
    ASSERT_NE(cp, nullptr);

    // Configure for guaranteed loss — press wrong every time, exhaust budget
    cp->getConfig().moveBudget = 5;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // In Show
    ASSERT_EQ(cp->getCurrentState()->getStateId(), CIPHER_SHOW);

    // Advance past show timer
    suite->tickWithTime(20, 100);

    // In Gameplay — press wrong buttons to exhaust budget
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
// EXPLOIT SEQUENCER E2E TESTS
// ============================================

/*
 * Test: Exploit Sequencer EASY win → unlocks B button.
 */
void e2eExploitSequencerEasyWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN for Exploit Sequencer (GameType 5, KonamiButton B=4)
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

    // Advance past show timer
    suite->tickWithTime(20, 100);

    // In Gameplay — advance symbol to within window, press
    suite->tickWithTime(5, 50);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    // Wait for evaluate and transition
    suite->tickWithTime(5, 100);

    ASSERT_EQ(es->getCurrentState()->getStateId(), EXPLOIT_WIN);

    // Advance past win timer
    suite->tickWithTime(35, 100);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // B button (bit 4) should be unlocked
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 4));
}

/*
 * Test: Exploit Sequencer HARD win → color profile eligibility.
 */
void e2eExploitSequencerHardWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::B));

    suite->triggerFdnHandshake("5", "4");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* es = suite->getExploitSequencer();
    ASSERT_NE(es, nullptr);

    // Configure for easy win
    es->getConfig().timingWindow = 45;
    es->getConfig().exploitsPerSeq = 1;
    es->getConfig().sequences = 1;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show timer
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
 * Test: Exploit Sequencer loss → no rewards.
 */
void e2eExploitSequencerLoss(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("5", "4");

    auto* es = suite->getExploitSequencer();
    ASSERT_NE(es, nullptr);

    // Configure for guaranteed loss — narrow window, press far from marker
    es->getConfig().scrollSpeedMs = 100;
    es->getConfig().markerPosition = 50;
    es->getConfig().timingWindow = 2;
    es->getConfig().failsAllowed = 0;

    // Advance past intro
    suite->tickWithTime(25, 100);

    // Advance past show timer
    suite->tickWithTime(20, 100);

    // Press immediately (symbol near 0, far from marker=50)
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

// ============================================
// BREACH DEFENSE E2E TESTS
// ============================================

/*
 * Test: Breach Defense EASY win → unlocks A button.
 */
void e2eBreachDefenseEasyWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN for Breach Defense (GameType 6, KonamiButton A=5)
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

    // Should be in Gameplay — match shield to threat lane
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

    // A button (bit 5) should be unlocked
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 5));
}

/*
 * Test: Breach Defense HARD win → color profile eligibility.
 */
void e2eBreachDefenseHardWin(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::A));

    suite->triggerFdnHandshake("6", "5");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickWithTime(3, 10);

    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);

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
    suite->tickWithTime(5, 10);

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have color profile eligibility
    suite->tickWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

/*
 * Test: Breach Defense loss → no rewards.
 */
void e2eBreachDefenseLoss(E2EGameSuiteTestSuite* suite) {
    suite->advanceToIdle();
    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("6", "5");

    auto* bd = suite->getBreachDefense();
    ASSERT_NE(bd, nullptr);

    // Configure for guaranteed loss — no misses allowed, wrong lane
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

    // Advance until threat arrives
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

#endif // NATIVE_BUILD
