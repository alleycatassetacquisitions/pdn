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
#include <cstdint>

using namespace cli;

// ============================================
// NEGATIVE FLOW TEST SUITE
//
// Tests for re-encounter logic, recreational mode,
// color UX improvements, and Idle palette indicator.
// Uses a Hunter player device by default.
// ============================================

class NegativeFlowTestSuite : public testing::Test {
public:
    void SetUp() override {
        hunter_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::SIGNAL_ECHO);
        SimpleTimer::setPlatformClock(hunter_.clockDriver);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(hunter_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            hunter_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void tickPlayerWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            hunter_.clockDriver->advance(delayMs);
            hunter_.pdn->loop();
        }
    }

    void advanceToIdle() {
        hunter_.game->skipToState(hunter_.pdn, 7);
        hunter_.pdn->loop();
    }

    int getPlayerStateId() {
        return hunter_.game->getCurrentState()->getStateId();
    }

    SignalEcho* getSignalEcho() {
        return dynamic_cast<SignalEcho*>(
            hunter_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    }

    void triggerFdnHandshake(const std::string& gameType, const std::string& reward) {
        std::string msg = "*fdn:" + gameType + ":" + reward + "\r";
        hunter_.serialOutDriver->injectInput(msg.c_str());
        tick(3);
        ASSERT_EQ(getPlayerStateId(), FDN_DETECTED);

        hunter_.serialOutDriver->injectInput("*fack\r");
        tickPlayerWithTime(5, 100);
    }

    void playSignalEchoToWin() {
        auto* echo = getSignalEcho();
        ASSERT_NE(echo, nullptr);

        // Advance past intro timer (2s)
        tickPlayerWithTime(5, 500);

        int numRounds = echo->getConfig().numSequences;
        for (int round = 0; round < numRounds; round++) {
            int seqLen = echo->getConfig().sequenceLength;
            int displayMs = echo->getConfig().displaySpeedMs;
            tickPlayerWithTime(seqLen * 3, displayMs);

            auto& session = echo->getSession();
            std::vector<bool> seq = session.currentSequence;
            for (bool isUp : seq) {
                if (isUp) {
                    hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                } else {
                    hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                }
                tickPlayerWithTime(1, 10);
            }
            tickPlayerWithTime(3, 10);
        }
    }

    DeviceInstance hunter_;
    DeviceInstance fdn_;
};

// ============================================
// BOUNTY FLOW TEST SUITE
//
// Separate suite that creates a Bounty player device
// to verify the FDN pipeline works for both roles.
// ============================================

class BountyFlowTestSuite : public testing::Test {
public:
    void SetUp() override {
        bounty_ = DeviceFactory::createDevice(0, false);  // false = bounty
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::SIGNAL_ECHO);
        SimpleTimer::setPlatformClock(bounty_.clockDriver);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(bounty_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            bounty_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void tickPlayerWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            bounty_.clockDriver->advance(delayMs);
            bounty_.pdn->loop();
        }
    }

    void advanceToIdle() {
        bounty_.game->skipToState(bounty_.pdn, 7);
        bounty_.pdn->loop();
    }

    int getPlayerStateId() {
        return bounty_.game->getCurrentState()->getStateId();
    }

    SignalEcho* getSignalEcho() {
        return dynamic_cast<SignalEcho*>(
            bounty_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    }

    /*
     * Bounty uses INPUT_JACK as active serial.
     * Inject FDN messages on serialInDriver.
     */
    void triggerFdnHandshake(const std::string& gameType, const std::string& reward) {
        std::string msg = "*fdn:" + gameType + ":" + reward + "\r";
        bounty_.serialInDriver->injectInput(msg.c_str());
        tick(3);
        ASSERT_EQ(getPlayerStateId(), FDN_DETECTED);

        bounty_.serialInDriver->injectInput("*fack\r");
        tickPlayerWithTime(5, 100);
    }

    void playSignalEchoToWin() {
        auto* echo = getSignalEcho();
        ASSERT_NE(echo, nullptr);

        tickPlayerWithTime(5, 500);

        int numRounds = echo->getConfig().numSequences;
        for (int round = 0; round < numRounds; round++) {
            int seqLen = echo->getConfig().sequenceLength;
            int displayMs = echo->getConfig().displaySpeedMs;
            tickPlayerWithTime(seqLen * 3, displayMs);

            auto& session = echo->getSession();
            std::vector<bool> seq = session.currentSequence;
            for (bool isUp : seq) {
                if (isUp) {
                    bounty_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                } else {
                    bounty_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                }
                tickPlayerWithTime(1, 10);
            }
            tickPlayerWithTime(3, 10);
        }
    }

    DeviceInstance bounty_;
    DeviceInstance fdn_;
};

// ============================================
// TEST 1: First encounter launches EASY directly
// No prior progression -> no FdnReencounter.
// ============================================
void firstEncounterLaunchesEasy(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->hunter_.player->hasUnlockedButton(
        static_cast<uint8_t>(KonamiButton::START)));

    // Trigger FDN (Signal Echo, reward START)
    suite->triggerFdnHandshake("7", "6");

    // Should go straight to Signal Echo (EASY), NOT FdnReencounter
    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_EASY.sequenceLength);
    ASSERT_TRUE(echo->getConfig().managedMode);

    // Recreational flag should be false (first encounter = progression)
    ASSERT_FALSE(suite->hunter_.player->isRecreationalMode());
}

// ============================================
// TEST 2: Re-encounter with button shows prompt
// Has START button (beat EASY) -> FdnReencounter.
// ============================================
void reencounterWithButtonShowsPrompt(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set: player has START button unlocked (beat EASY before)
    suite->hunter_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    // Trigger FDN
    suite->triggerFdnHandshake("7", "6");

    // Should be in FdnReencounter state
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Display should show options (text history holds last 4 entries;
    // game name "SIGNAL ECHO" is drawn first and gets evicted)
    const auto& textHistory = suite->hunter_.displayDriver->getTextHistory();
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
}

// ============================================
// TEST 3: Re-encounter choose HARD launches HARD
// Select HARD at FdnReencounter -> HARD config.
// ============================================
void reencounterChooseHardLaunchesHard(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();
    suite->hunter_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Cursor starts at HARD (index 0). Press PRIMARY to confirm.
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Should have launched Signal Echo with HARD config
    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_HARD.sequenceLength);
    ASSERT_EQ(echo->getConfig().allowedMistakes, SIGNAL_ECHO_HARD.allowedMistakes);
    ASSERT_TRUE(echo->getConfig().managedMode);

    // Should NOT be recreational (earning color profile)
    ASSERT_FALSE(suite->hunter_.player->isRecreationalMode());
}

// ============================================
// TEST 4: Re-encounter choose EASY sets recreational
// Select EASY at FdnReencounter -> recreational mode.
// ============================================
void reencounterChooseEasyLaunchesRecreational(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();
    suite->hunter_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Cycle to EASY (index 1): press SECONDARY once
    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);

    // Confirm with PRIMARY
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Should have launched Signal Echo with EASY config
    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_EASY.sequenceLength);

    // EASY on a re-encounter = recreational
    ASSERT_TRUE(suite->hunter_.player->isRecreationalMode());
}

// ============================================
// TEST 5: Re-encounter choose SKIP returns to Idle
// Select SKIP at FdnReencounter -> Idle.
// ============================================
void reencounterChooseSkipReturnsToIdle(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();
    suite->hunter_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Cycle to SKIP (index 2): press SECONDARY twice
    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);
    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);

    // Confirm SKIP with PRIMARY
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Should be back at Idle
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
}

// ============================================
// TEST 6: Fully completed re-encounter, all recreational
// Has button + profile -> all choices are recreational.
// ============================================
void fullyCompletedReencounterAllRecreational(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();
    suite->hunter_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));
    suite->hunter_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Verify fullyCompleted state: both button + profile means
    // HARD choice should still be recreational.
    // (text history holds last 4 entries; "COMPLETED!" is drawn early and gets evicted,
    // so we verify behavior instead of display text)

    // Choose HARD (index 0) -- still recreational because fully completed
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_TRUE(suite->hunter_.player->isRecreationalMode());
}

// ============================================
// TEST 7: Re-encounter timeout defaults to SKIP
// 15s auto-dismiss returns to Idle.
// ============================================
void reencounterTimeoutDefaultsToSkip(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();
    suite->hunter_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Wait 15+ seconds (30 ticks * 600ms = 18s)
    suite->tickPlayerWithTime(30, 600);

    // Should auto-dismiss to Idle
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
}

// ============================================
// TEST 8: Recreational win skips rewards
// Recreational mode win does not unlock button or profile.
// ============================================
void recreationalWinSkipsRewards(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set: has START button
    suite->hunter_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));
    uint8_t progressBefore = suite->hunter_.player->getKonamiProgress();

    // Trigger re-encounter
    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose EASY (recreational): cycle to index 1, confirm
    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_TRUE(suite->hunter_.player->isRecreationalMode());

    // Play Signal Echo to win
    suite->playSignalEchoToWin();

    // Wait for win timer -> returnToPreviousApp -> FdnComplete
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Konami progress should be unchanged (no new button)
    ASSERT_EQ(suite->hunter_.player->getKonamiProgress(), progressBefore);

    // No color profile eligibility earned
    ASSERT_FALSE(suite->hunter_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    // No pending profile game (no color prompt routing)
    ASSERT_EQ(suite->hunter_.player->getPendingProfileGame(), -1);

    // Timer -> should go to Idle (not ColorProfilePrompt)
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), IDLE);

    // Recreational flag should be cleared by FdnComplete dismount
    ASSERT_FALSE(suite->hunter_.player->isRecreationalMode());
}

// ============================================
// TEST 9: Color prompt first profile shows "EQUIP?"
// No current equipped profile -> "EQUIP?" text.
// ============================================
void colorPromptFirstProfileShowsEquip(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();

    // Ensure no equipped profile
    ASSERT_EQ(suite->hunter_.player->getEquippedColorProfile(), -1);

    // Set pending profile for prompt
    suite->hunter_.player->setPendingProfileGame(static_cast<int>(GameType::SIGNAL_ECHO));

    // Skip to ColorProfilePrompt (index 24 in new stateMap)
    suite->hunter_.game->skipToState(suite->hunter_.pdn, 24);
    suite->hunter_.pdn->loop();

    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);

    // Display should show "EQUIP?" text
    const auto& textHistory = suite->hunter_.displayDriver->getTextHistory();
    bool foundEquip = false;
    for (const auto& entry : textHistory) {
        if (entry.find("EQUIP?") != std::string::npos) foundEquip = true;
    }
    ASSERT_TRUE(foundEquip);
}

// ============================================
// TEST 10: Color prompt with existing shows "SWAP?"
// Has equipped profile -> "SWAP?" + current name.
// ============================================
void colorPromptWithExistingShowsSwap(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();

    // Set existing equipped profile
    suite->hunter_.player->setEquippedColorProfile(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->hunter_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->hunter_.player->addColorProfileEligibility(static_cast<int>(GameType::FIREWALL_DECRYPT));
    suite->hunter_.player->setPendingProfileGame(static_cast<int>(GameType::FIREWALL_DECRYPT));

    // Skip to ColorProfilePrompt (index 24)
    suite->hunter_.game->skipToState(suite->hunter_.pdn, 24);
    suite->hunter_.pdn->loop();

    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);

    // Display should show "SWAP?" and current profile name
    const auto& textHistory = suite->hunter_.displayDriver->getTextHistory();
    bool foundSwap = false;
    bool foundCurrentName = false;
    for (const auto& entry : textHistory) {
        if (entry.find("SWAP?") != std::string::npos) foundSwap = true;
        if (entry.find("SIGNAL ECHO") != std::string::npos) foundCurrentName = true;
    }
    ASSERT_TRUE(foundSwap);
    ASSERT_TRUE(foundCurrentName);
}

// ============================================
// TEST 11: Picker UP button equips profile
// PRIMARY (UP) = equip after button swap.
// ============================================
void pickerUpButtonEquipsProfile(NegativeFlowTestSuite* suite) {
    // Set eligibility BEFORE advancing to Idle
    suite->hunter_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->advanceToIdle();

    // Long press secondary -> picker
    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PICKER);

    // Profile list: [SIGNAL_ECHO(7), DEFAULT(-1)]
    // equipped=-1, pre-select finds DEFAULT at index 1, cursor=1
    // Cycle to SIGNAL_ECHO (index 0) using SECONDARY
    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);

    // Equip with PRIMARY (new equip button)
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_EQ(suite->hunter_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
}

// ============================================
// TEST 12: Picker DOWN button cycles profile
// SECONDARY (DOWN) = cycle after button swap.
// ============================================
void pickerDownButtonCyclesProfile(NegativeFlowTestSuite* suite) {
    suite->hunter_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->hunter_.player->addColorProfileEligibility(static_cast<int>(GameType::FIREWALL_DECRYPT));

    suite->advanceToIdle();

    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PICKER);

    // Profile list (std::set<int> sorted): [FIREWALL_DECRYPT(3), SIGNAL_ECHO(7), DEFAULT(-1)]
    // equipped=-1, pre-select finds DEFAULT at index 2, cursor=2
    // Cycle once: 2->0 (FIREWALL_DECRYPT)
    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);

    // Now at index 0 (FIREWALL_DECRYPT). Equip with PRIMARY.
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
    ASSERT_EQ(suite->hunter_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::FIREWALL_DECRYPT));
}

// ============================================
// TEST 13: Picker shows role-aware default name
// Display shows "HUNTER DEFAULT" not "DEFAULT".
// ============================================
void pickerShowsRoleAwareDefaultName(NegativeFlowTestSuite* suite) {
    suite->hunter_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->advanceToIdle();

    suite->hunter_.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
    suite->tickPlayerWithTime(3, 10);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PICKER);

    // Display should show "HUNTER DEFAULT" (not just "DEFAULT")
    const auto& textHistory = suite->hunter_.displayDriver->getTextHistory();
    bool foundHunterDefault = false;
    for (const auto& entry : textHistory) {
        if (entry.find("HUNTER DEFAULT") != std::string::npos) foundHunterDefault = true;
    }
    ASSERT_TRUE(foundHunterDefault);
}

// ============================================
// TEST 14: Idle shows palette indicator
// Default profile -> "Palette:HUNTER DEFAULT".
// ============================================
void idleShowsPaletteIndicator(NegativeFlowTestSuite* suite) {
    suite->advanceToIdle();

    // Trigger a stats cycle to render
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Display should show "Palette:HUNTER DEFAULT"
    const auto& textHistory = suite->hunter_.displayDriver->getTextHistory();
    bool foundPalette = false;
    for (const auto& entry : textHistory) {
        if (entry.find("Palette:HUNTER DEFAULT") != std::string::npos) foundPalette = true;
    }
    ASSERT_TRUE(foundPalette);
}

// ============================================
// TEST 15: Idle shows equipped palette name
// Equipped Signal Echo -> "Palette:SIGNAL ECHO".
// ============================================
void idleShowsEquippedPaletteName(NegativeFlowTestSuite* suite) {
    suite->hunter_.player->setEquippedColorProfile(static_cast<int>(GameType::SIGNAL_ECHO));

    suite->advanceToIdle();

    // Trigger a stats cycle
    suite->hunter_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    const auto& textHistory = suite->hunter_.displayDriver->getTextHistory();
    bool foundPalette = false;
    for (const auto& entry : textHistory) {
        if (entry.find("Palette:SIGNAL ECHO") != std::string::npos) foundPalette = true;
    }
    ASSERT_TRUE(foundPalette);
}

// ============================================
// TEST 16: Bounty FDN easy win
// Bounty player first encounter -> EASY win -> Konami button.
// ============================================
void bountyFdnEasyWin(BountyFlowTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_FALSE(suite->bounty_.player->hasUnlockedButton(
        static_cast<uint8_t>(KonamiButton::START)));

    suite->triggerFdnHandshake("7", "6");

    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_EASY.sequenceLength);
    ASSERT_TRUE(echo->getConfig().managedMode);

    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer -> FdnComplete

    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // START button (bit 6) should be unlocked
    uint8_t progress = suite->bounty_.player->getKonamiProgress();
    ASSERT_TRUE(progress & (1 << 6));
}

// ============================================
// TEST 17: Bounty HARD win gets color prompt
// Bounty re-encounters, plays HARD, wins -> color prompt.
// ============================================
void bountyFdnHardWinColorPrompt(BountyFlowTestSuite* suite) {
    suite->advanceToIdle();

    // Pre-set: has START button (beat EASY before)
    suite->bounty_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose HARD (index 0), confirm
    suite->bounty_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    // Should have launched HARD
    auto* echo = suite->getSignalEcho();
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(echo->getConfig().sequenceLength, SIGNAL_ECHO_HARD.sequenceLength);

    // Play to win
    suite->playSignalEchoToWin();
    suite->tickPlayerWithTime(10, 400);  // Win timer -> FdnComplete
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);

    // Should have earned color profile eligibility
    ASSERT_TRUE(suite->bounty_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));

    // Timer -> ColorProfilePrompt
    suite->tickPlayerWithTime(10, 400);
    ASSERT_EQ(suite->getPlayerStateId(), COLOR_PROFILE_PROMPT);
}

// ============================================
// TEST 18: Bounty re-encounter prompt and SKIP
// Bounty re-encounters, sees prompt, chooses SKIP.
// ============================================
void bountyReencounterPrompt(BountyFlowTestSuite* suite) {
    suite->advanceToIdle();
    suite->bounty_.player->unlockKonamiButton(static_cast<uint8_t>(KonamiButton::START));

    suite->triggerFdnHandshake("7", "6");
    ASSERT_EQ(suite->getPlayerStateId(), FDN_REENCOUNTER);

    // Choose SKIP (cycle twice, then confirm)
    suite->bounty_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);
    suite->bounty_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(1, 10);
    suite->bounty_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tickPlayerWithTime(3, 10);

    ASSERT_EQ(suite->getPlayerStateId(), IDLE);
}

#endif // NATIVE_BUILD
