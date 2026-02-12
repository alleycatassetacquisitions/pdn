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
// E2E PROGRESSION TEST SUITE
//
// Full end-to-end tests covering the entire FDN
// progression loop: Idle → FDN → minigame → FdnComplete
// → Konami button → auto-boon → color profile → equip.
// ============================================

class ProgressionE2ETestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::SIGNAL_ECHO);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            fdn_.pdn->loop();
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
    // reward: "6" for START, "2" for LEFT
    void triggerFdnHandshake(const std::string& gameType, const std::string& reward) {
        std::string msg = "*fdn:" + gameType + ":" + reward + "\r";
        player_.serialOutDriver->injectInput(msg.c_str());
        tick(3);
        ASSERT_EQ(getPlayerStateId(), FDN_DETECTED);

        player_.serialOutDriver->injectInput("*fack\r");
        tickPlayerWithTime(5, 100);
    }

    // Helper: play Signal Echo to completion (win).
    // Advances through intro, show sequence, player input for all rounds.
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

    // Helper: play Firewall Decrypt to completion (win).
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

    // Helper: play Signal Echo and intentionally lose (press wrong every time)
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

    DeviceInstance player_;
    DeviceInstance fdn_;
};

// ============================================
// TEST: Full Signal Echo easy win → Konami button
// Steps 1-3: Connect to FDN, beat EASY, win START button
// ============================================
void e2eSignalEchoEasyWin(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->player_.player->hasKonamiBoon());

    // Step 1: Trigger FDN (Signal Echo, reward START)
    suite->triggerFdnHandshake("7", "6");

    // Verify EASY config (no boon → easy)
    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_EASY.sequenceLength);
    ASSERT_TRUE(echo->getConfig().managedMode);

    // Step 2: Play to win
    suite->playSignalEchoToWin();

    // Should be in EchoWin
    auto* echoSM = suite->getSignalEcho();
    ASSERT_EQ(echoSM->getOutcome().result, MiniGameResult::WON);

    // Wait for win timer, returnToPreviousApp → FdnComplete
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Step 3: Konami button START (bit 6) should be unlocked
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 6));  // START = bit 6
}

// ============================================
// TEST: Auto-boon triggers on 7th button
// Step 4: Set 6 other buttons + win again → auto-boon
// ============================================
void e2eAutoBoonOnSeventhButton(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set 6 of 7 buttons (all except START = bit 6)
    suite->player_.player->setKonamiProgress(0x3F);
    ASSERT_FALSE(suite->player_.player->isKonamiComplete());

    // Trigger Signal Echo, win → unlocks START (7th button)
    suite->triggerFdnHandshake("7", "6");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer → FdnComplete

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
    ASSERT_TRUE(suite->player_.player->isKonamiComplete());
    ASSERT_TRUE(suite->player_.player->hasKonamiBoon());
}

// ============================================
// TEST: Server sync on progress save
// Step 5: Verify PUT /api/progress called
// ============================================
void e2eServerSyncOnWin(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();

    suite->triggerFdnHandshake("7", "6");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete

    // Check HTTP history for PUT /api/progress
    const auto& history = MockHttpServer::getInstance().getHistory();
    bool foundSync = false;
    for (const auto& entry : history) {
        if (entry.method == "PUT" && entry.path == "/api/progress") {
            foundSync = true;
        }
    }
    ASSERT_TRUE(foundSync);
}

// ============================================
// TEST: Hard mode after boon, color prompt on win
// Steps 6-8: Reconnect with boon → HARD → win → prompt → YES → equip
// ============================================
void e2eHardModeColorEquip(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->setKonamiBoon(true);

    // Step 6: Reconnect to FDN
    suite->triggerFdnHandshake("7", "6");

    // Step 7: Verify HARD config
    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_HARD.sequenceLength);
    ASSERT_TRUE(echo->getConfig().managedMode);

    // Play to win
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete

    // FdnComplete should set pendingProfileGame
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Wait for FdnComplete display timer → ColorProfilePrompt
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);

    // Step 8: Select YES (default) + confirm
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Should be back at Idle with profile equipped
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_EQ(suite->player_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
}

// ============================================
// TEST: Color prompt NO → palette NOT equipped
// Step 8 alt: select NO → Idle without equip
// ============================================
void e2eColorPromptDecline(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->setKonamiBoon(true);

    suite->triggerFdnHandshake("7", "6");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete
    suite->tickPlayerWithTime(10, 400);  // → ColorProfilePrompt
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);

    // Toggle to NO, then confirm
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    // Profile NOT equipped
    ASSERT_EQ(suite->player_.player->getEquippedColorProfile(), -1);
    // But eligibility is still there for later
    ASSERT_TRUE(suite->player_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));
}

// ============================================
// TEST: Color picker from Idle (long press)
// Steps 10-11: Long press → picker → scroll → equip/unequip
// ============================================
void e2eColorPickerFromIdle(ProgressionE2ETestSuite* suite) {
    // Set eligibility BEFORE advancing to Idle (long press is registered during mount)
    suite->player_.player->addColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO));
    suite->player_.player->setEquippedColorProfile(
        static_cast<int>(GameType::SIGNAL_ECHO));

    suite->advanceToIdle();

    // Long press secondary → picker
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PICKER);

    // Profile list: [SIGNAL_ECHO, DEFAULT]
    // Pre-selected: SIGNAL_ECHO (index 0, since it's equipped)
    // Scroll to DEFAULT (index 1)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);

    // Confirm → equip DEFAULT (-1)
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_EQ(suite->player_.player->getEquippedColorProfile(), -1);
}

// ============================================
// TEST: Easy mode loss → no Konami button, no progression
// ============================================
void e2eEasyModeLoss(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();

    uint8_t progressBefore = suite->player_.player->getKonamiProgress();

    suite->triggerFdnHandshake("7", "6");
    suite->playSignalEchoToLose();

    // Wait for lose timer → FdnComplete
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No Konami button unlocked
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), progressBefore);
    ASSERT_FALSE(suite->player_.player->hasKonamiBoon());

    // FdnComplete timer → back to Idle (no prompt)
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
}

// ============================================
// TEST: Hard mode loss → no color eligibility
// ============================================
void e2eHardModeLoss(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->setKonamiBoon(true);

    suite->triggerFdnHandshake("7", "6");
    suite->playSignalEchoToLose();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // No color eligibility on loss
    ASSERT_FALSE(suite->player_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    // No pending profile → goes straight to Idle
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
}

// ============================================
// TEST: Firewall Decrypt E2E — same progression flow
// ============================================
void e2eFirewallDecryptProgression(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();

    // Trigger FDN with FIREWALL_DECRYPT (GameType 3, reward LEFT=2)
    suite->triggerFdnHandshake("3", "2");

    auto* fw = suite->getFirewallDecrypt();
    ASSERT_NE(fw, nullptr);
    ASSERT_EQ(fw->getConfig().numCandidates, FIREWALL_DECRYPT_EASY.numCandidates);
    ASSERT_TRUE(fw->getConfig().managedMode);

    // Play to win
    suite->playFirewallDecryptToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // LEFT button (bit 2) should be unlocked
    uint8_t progress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 2));
}

// ============================================
// TEST: Firewall Decrypt hard mode with boon
// ============================================
void e2eFirewallDecryptHard(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->setKonamiBoon(true);

    suite->triggerFdnHandshake("3", "2");

    auto* fw = suite->getFirewallDecrypt();
    ASSERT_NE(fw, nullptr);
    ASSERT_EQ(fw->getConfig().numCandidates, FIREWALL_DECRYPT_HARD.numCandidates);
    ASSERT_EQ(fw->getConfig().numRounds, FIREWALL_DECRYPT_HARD.numRounds);
    ASSERT_TRUE(fw->getConfig().managedMode);
}

// ============================================
// TEST: NVS round-trip (save → load into fresh player)
// ============================================
void e2eNvsRoundTrip(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();

    // Set some progression state
    suite->player_.player->setKonamiProgress(0x3F);
    suite->player_.player->setKonamiBoon(true);
    suite->player_.player->setEquippedColorProfile(
        static_cast<int>(GameType::SIGNAL_ECHO));
    suite->player_.player->addColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO));

    // Save via a ProgressManager
    ProgressManager pm;
    pm.initialize(suite->player_.player, suite->player_.storageDriver);
    pm.saveProgress();

    // Load into a fresh player to verify persistence
    Player freshPlayer;
    ProgressManager pm2;
    pm2.initialize(&freshPlayer, suite->player_.storageDriver);
    pm2.loadProgress();

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 0x3F);
    ASSERT_TRUE(freshPlayer.hasKonamiBoon());
    ASSERT_EQ(freshPlayer.getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
    ASSERT_TRUE(freshPlayer.hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));
}

// ============================================
// TEST: Prompt auto-dismiss defaults to NO
// ============================================
void e2ePromptAutoDismiss(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->setKonamiBoon(true);

    suite->triggerFdnHandshake("7", "6");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete
    suite->tickPlayerWithTime(10, 400);  // → ColorProfilePrompt
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);

    // Wait for auto-dismiss (10s)
    suite->tickPlayerWithTime(25, 500);

    // Should be at Idle, profile NOT equipped
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_EQ(suite->player_.player->getEquippedColorProfile(), -1);
}

// ============================================
// TEST: Difficulty gating is dynamic (switches on boon)
// ============================================
void e2eDifficultyGatingDynamic(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();

    // First encounter — EASY
    ASSERT_FALSE(suite->player_.player->hasKonamiBoon());
    suite->triggerFdnHandshake("7", "6");
    auto* echo1 = suite->getSignalEcho();
    ASSERT_EQ(echo1->getConfig().sequenceLength, SIGNAL_ECHO_EASY.sequenceLength);

    // Play to win, get through FdnComplete
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer
    suite->tickPlayerWithTime(10, 400);  // FdnComplete → Idle
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Grant boon
    suite->player_.player->setKonamiBoon(true);

    // Second encounter — should be HARD now
    suite->triggerFdnHandshake("7", "6");
    auto* echo2 = suite->getSignalEcho();
    ASSERT_EQ(echo2->getConfig().sequenceLength, SIGNAL_ECHO_HARD.sequenceLength);
}

// ============================================
// TEST: Equip later via picker after declining prompt
// ============================================
void e2eEquipLaterViaPicker(ProgressionE2ETestSuite* suite) {
    suite->advanceToIdle();
    suite->player_.player->setKonamiBoon(true);

    // Win hard mode, decline at prompt
    suite->triggerFdnHandshake("7", "6");
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // → FdnComplete
    suite->tickPlayerWithTime(10, 400);  // → ColorProfilePrompt

    // Toggle to NO, confirm
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_EQ(suite->player_.player->getEquippedColorProfile(), -1);

    // Long press → picker
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PICKER);

    // Picker list: [SIGNAL_ECHO, DEFAULT]
    // Pre-selected: DEFAULT (index 1, since equippedColorProfile=-1)
    // Scroll to SIGNAL_ECHO (index 0)
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);

    // Confirm → equip Signal Echo
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_EQ(suite->player_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
}

#endif // NATIVE_BUILD
