#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/color-profiles.hpp"
#include "game/minigame.hpp"
#include "game/progress-manager.hpp"
#include "device/device-constants.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// HARD MODE RE-ENCOUNTER TEST SUITE
//
// Tests for edge cases in the hard mode re-encounter flow:
// - Full completion (easy + hard) → re-encounter shows completed
// - Hard mode loss → retry with HARD option still available
// - Mixed progress across multiple games
// - Recreational mode verification (no double rewards)
// ============================================

class HardModeReencounterTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        fdnSignalEcho_ = DeviceFactory::createFdnDevice(1, GameType::SIGNAL_ECHO);
        fdnFirewall_ = DeviceFactory::createFdnDevice(2, GameType::FIREWALL_DECRYPT);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(fdnFirewall_);
        DeviceFactory::destroyDevice(fdnSignalEcho_);
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            fdnSignalEcho_.pdn->loop();
            fdnFirewall_.pdn->loop();
        }
    }

    void tickPlayerWithTime(int n, int delayMs) {
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

    SignalEcho* getSignalEcho() {
        return dynamic_cast<SignalEcho*>(
            player_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    }

    FirewallDecrypt* getFirewallDecrypt() {
        return dynamic_cast<FirewallDecrypt*>(
            player_.pdn->getApp(StateId(FIREWALL_DECRYPT_APP_ID)));
    }

    // Helper: trigger FDN handshake from Idle
    // gameType: "7" for SIGNAL_ECHO, "3" for FIREWALL_DECRYPT
    // reward: "0" for UP/START, "2" for LEFT
    void triggerFdnHandshake(const std::string& gameType, const std::string& reward) {
        std::string msg = "*fdn:" + gameType + ":" + reward + "\r";
        player_.serialOutDriver->injectInput(msg.c_str());
        tick(3);
        ASSERT_EQ(getPlayerStateId(), FDN_DETECTED);

        player_.serialOutDriver->injectInput("*fack\r");
        tickPlayerWithTime(5, 100);
    }

    // Helper: play Signal Echo to win
    void playSignalEchoToWin() {
        auto* echo = getSignalEcho();
        ASSERT_NE(echo, nullptr);

        // Advance past intro timer (2s)
        tickPlayerWithTime(5, 500);

        int numRounds = echo->getConfig().numSequences;
        for (int round = 0; round < numRounds; round++) {
            // In ShowSequence state — advance past all signal displays
            int seqLen = echo->getConfig().sequenceLength;
            int displayMs = echo->getConfig().displaySpeedMs;
            tickPlayerWithTime(seqLen * 3, displayMs);

            // Now in PlayerInput — press the correct sequence
            auto& session = echo->getSession();
            std::vector<bool> seq = session.currentSequence;
            for (bool isUp : seq) {
                if (isUp) {
                    player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                } else {
                    player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                }
                tickPlayerWithTime(1, 10);
            }

            // Tick to process evaluate + next round transition
            tickPlayerWithTime(3, 10);
        }
    }

    // Helper: play Signal Echo and intentionally lose
    void playSignalEchoToLose() {
        auto* echo = getSignalEcho();
        ASSERT_NE(echo, nullptr);

        // Advance past intro
        tickPlayerWithTime(5, 500);

        // In ShowSequence — advance past signal displays
        int seqLen = echo->getConfig().sequenceLength;
        int displayMs = echo->getConfig().displaySpeedMs;
        tickPlayerWithTime(seqLen * 3, displayMs);

        // In PlayerInput — press wrong buttons until mistakes exceed allowed
        auto& session = echo->getSession();
        int allowed = echo->getConfig().allowedMistakes;
        for (int i = 0; i <= allowed + 1; i++) {
            bool expected = session.currentSequence[i % seqLen];
            // Press the opposite of what's expected
            if (expected) {
                player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            } else {
                player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            }
            tickPlayerWithTime(1, 10);
        }
        tickPlayerWithTime(3, 10);
    }

    // Helper: play Firewall Decrypt to win
    void playFirewallDecryptToWin() {
        auto* fw = getFirewallDecrypt();
        ASSERT_NE(fw, nullptr);

        // Advance past intro timer (2s)
        tickPlayerWithTime(5, 500);

        int numRounds = fw->getConfig().numRounds;
        for (int round = 0; round < numRounds; round++) {
            auto& session = fw->getSession();

            // Scroll to target index
            for (int i = 0; i < session.targetIndex; i++) {
                player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                tickPlayerWithTime(1, 10);
            }

            // Confirm correct selection
            player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            tickPlayerWithTime(3, 10);
        }
    }

    DeviceInstance player_;
    DeviceInstance fdnSignalEcho_;
    DeviceInstance fdnFirewall_;
};

// ============================================
// TEST 1: Full completion re-encounter (easy + hard won)
// Win easy → win hard → re-encounter shows "COMPLETED!"
// All choices (HARD/EASY) are recreational mode
// ============================================
void fullCompletionReencounter(HardModeReencounterTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasKonamiBoon());

    // Step 1: First encounter — win EASY
    suite->triggerFdnHandshake("7", "0");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer → FdnComplete
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Konami button unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(
        static_cast<uint8_t>(KonamiButton::START)));

    // Return to Idle
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Step 2: Second encounter — choose HARD, win
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD (index 0)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer → returnToPreviousApp
    suite->tickPlayerWithTime(5, 10);    // FdnReencounter → FdnComplete transition
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Color profile eligibility granted
    suite->tickPlayerWithTime(10, 400);  // FdnComplete → ColorProfilePrompt
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);

    // Accept color profile
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Verify eligibility is set
    ASSERT_TRUE(suite->player_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    // Step 3: Third encounter — fully completed
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Verify HARD choice is recreational (no progression)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_TRUE(suite->player_.player->isRecreationalMode());
}

// ============================================
// TEST 2: Recreational HARD mode grants no new rewards
// Fully completed → play HARD again → win → no color eligibility added again
// ============================================
void recreationalHardModeNoRewards(HardModeReencounterTestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set: fully completed (button + color eligibility)
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));
    suite->player_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    // Trigger re-encounter
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD (recreational)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_TRUE(suite->player_.player->isRecreationalMode());

    // Play to win
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer
    suite->tickPlayerWithTime(5, 10);    // FdnReencounter → FdnComplete
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No new progression
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);

    // No pending profile (already have eligibility)
    ASSERT_EQ(suite->player_.player->getPendingProfileGame(), -1);

    // Should go straight to Idle (no ColorProfilePrompt)
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Recreational flag cleared
    ASSERT_FALSE(suite->player_.player->isRecreationalMode());
}

// ============================================
// TEST 3: Hard mode loss → retry still shows HARD option
// Win easy → lose hard → re-encounter still shows HARD/EASY/SKIP
// Color profile NOT unlocked after loss
// ============================================
void hardModeLossRetryShowsHardOption(HardModeReencounterTestSuite* suite) {
    suite->advanceToIdle();

    // Step 1: Win EASY first
    suite->triggerFdnHandshake("7", "0");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete
    suite->tickPlayerWithTime(10, 400);  // → Idle
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Step 2: Attempt HARD and lose
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Lose
    suite->playSignalEchoToLose();
    suite->tickPlayerWithTime(10, 400);  // Lose timer → returnToPreviousApp
    suite->tickPlayerWithTime(5, 10);    // FdnReencounter → FdnComplete
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No color eligibility on loss
    ASSERT_FALSE(suite->player_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    // Return to Idle
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Step 3: Re-encounter again — HARD option should still be available
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Verify HARD option is present (text history)
    const auto& textHistory = suite->player_.displayDriver->getTextHistory();
    bool foundHard = false;
    bool foundEasy = false;
    bool foundSkip = false;
    for (const auto& entry : textHistory) {
        if (entry.find("HARD") != std::string::npos) foundHard = true;
        if (entry.find("EASY") != std::string::npos) foundEasy = true;
        if (entry.find("SKIP") != std::string::npos) foundSkip = true;
    }
    ASSERT_TRUE(foundHard);
    ASSERT_TRUE(foundEasy);
    ASSERT_TRUE(foundSkip);

    // Step 4: Choose HARD again and win
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer
    suite->tickPlayerWithTime(5, 10);    // → FdnComplete
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // NOW color eligibility should be granted
    suite->tickPlayerWithTime(10, 400);  // → ColorProfilePrompt
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);

    // Accept and verify
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_TRUE(suite->player_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));
}

// ============================================
// TEST 4: Mixed progress across multiple games
// Win easy on Game A and Game B
// Win hard on Game A only
// Re-encounter Game A → completed, Game B → shows "beaten easy" status
// ============================================
void mixedProgressAcrossGames(HardModeReencounterTestSuite* suite) {
    suite->advanceToIdle();

    // Game A = Signal Echo (GameType 7, reward UP/START=0/6)
    // Game B = Firewall Decrypt (GameType 3, reward LEFT=2)

    // Step 1: Win EASY on Game A (Signal Echo)
    suite->triggerFdnHandshake("7", "0");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete
    suite->tickPlayerWithTime(10, 400);  // → Idle
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Step 2: Win EASY on Game B (Firewall Decrypt)
    suite->triggerFdnHandshake("3", "2");
    suite->playFirewallDecryptToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete
    suite->tickPlayerWithTime(10, 400);  // → Idle
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Step 3: Win HARD on Game A only
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer
    suite->tickPlayerWithTime(5, 10);    // → FdnComplete
    suite->tickPlayerWithTime(10, 400);  // → ColorProfilePrompt

    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Verify Game A fully completed
    ASSERT_TRUE(suite->player_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    // Step 4: Re-encounter Game A — should show completed
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // All choices should be recreational
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_TRUE(suite->player_.player->isRecreationalMode());

    // Skip back to Idle
    suite->tickPlayerWithTime(10, 400);  // Win timer (or just advance)
    suite->tickPlayerWithTime(5, 10);
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Step 5: Re-encounter Game B — NOT completed (only easy won)
    suite->triggerFdnHandshake("3", "2");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Should show HARD/EASY/SKIP with HARD being progression (not recreational)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // HARD mode on Game B should NOT be recreational
    ASSERT_FALSE(suite->player_.player->isRecreationalMode());
}

// ============================================
// TEST 5: Recreational EASY mode also grants no rewards
// Fully completed → play EASY again → win → no button unlock attempted
// ============================================
void recreationalEasyModeNoRewards(HardModeReencounterTestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set: fully completed
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));
    suite->player_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    // Trigger re-encounter
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose EASY (index 1)
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_TRUE(suite->player_.player->isRecreationalMode());

    // Play to win
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No progression change
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);

    // No pending profile
    ASSERT_EQ(suite->player_.player->getPendingProfileGame(), -1);

    // Return to Idle
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_FALSE(suite->player_.player->isRecreationalMode());
}

// ============================================
// TEST 6: No NVS writes in recreational mode
// Fully completed → recreational win → verify ProgressManager not triggered
// ============================================
void recreationalModeNoNvsWrites(HardModeReencounterTestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set: fully completed
    suite->player_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));
    suite->player_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    // Clear HTTP history
    MockHttpServer::getInstance().clearHistory();

    // Trigger re-encounter, choose HARD (recreational)
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Play to win
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer
    suite->tickPlayerWithTime(5, 10);    // → FdnComplete

    // Wait for FdnComplete timer
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Check HTTP history — should NOT have PUT /api/progress for recreational win
    const auto& history = MockHttpServer::getInstance().getHistory();
    int progressSyncs = 0;
    for (const auto& entry : history) {
        if (entry.method == "PUT" && entry.path == "/api/progress") {
            progressSyncs++;
        }
    }

    // Recreational mode should not trigger progress sync
    ASSERT_EQ(progressSyncs, 0);
}

// ============================================
// TEST 7: Konami progress unchanged after recreational win
// Fully completed → recreational HARD win → verify exact same Konami bits
// ============================================
void konamiProgressUnchangedAfterRecreational(HardModeReencounterTestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set: fully completed with specific Konami pattern
    suite->player_.player->setKonamiProgress(0x40);  // Only START button (bit 6)
    suite->player_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    uint8_t exactProgressBefore = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(exactProgressBefore, 0x40);

    // Trigger re-encounter
    suite->triggerFdnHandshake("7", "0");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD (recreational)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Play to win
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);
    suite->tickPlayerWithTime(5, 10);
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Konami progress should be EXACTLY the same
    uint8_t exactProgressAfter = suite->player_.player->getKonamiProgress();
    ASSERT_EQ(exactProgressAfter, 0x40);
}

#endif // NATIVE_BUILD
