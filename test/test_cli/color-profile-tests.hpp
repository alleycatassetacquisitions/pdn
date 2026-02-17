#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/color-profiles.hpp"
#include "game/progress-manager.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// COLOR PROFILE LOOKUP TEST SUITE
// ============================================

class ColorProfileLookupTestSuite : public testing::Test {};

// Test: getColorProfileState returns Signal Echo palette
void colorProfileLookupSignalEcho(ColorProfileLookupTestSuite* /*suite*/) {
    const LEDState& state = getColorProfileState(static_cast<int>(GameType::SIGNAL_ECHO));
    // Signal Echo palette: first LED should be magenta (255,0,255)
    ASSERT_EQ(state.leftLights[0].color.red, 255);
    ASSERT_EQ(state.leftLights[0].color.green, 0);
    ASSERT_EQ(state.leftLights[0].color.blue, 255);
}

// Test: getColorProfileState returns Firewall Decrypt palette
void colorProfileLookupFirewallDecrypt(ColorProfileLookupTestSuite* /*suite*/) {
    const LEDState& state = getColorProfileState(static_cast<int>(GameType::FIREWALL_DECRYPT));
    // Firewall Decrypt palette: first LED should be green (0,255,100)
    ASSERT_EQ(state.leftLights[0].color.red, 0);
    ASSERT_EQ(state.leftLights[0].color.green, 255);
    ASSERT_EQ(state.leftLights[0].color.blue, 100);
}

// Test: getColorProfileState returns default for unknown game type
void colorProfileLookupDefault(ColorProfileLookupTestSuite* /*suite*/) {
    const LEDState& state = getColorProfileState(-1);
    // Default palette: warm yellow (255,255,100)
    ASSERT_EQ(state.leftLights[0].color.red, 255);
    ASSERT_EQ(state.leftLights[0].color.green, 255);
    ASSERT_EQ(state.leftLights[0].color.blue, 100);
}

// Test: getColorProfileName returns correct names
void colorProfileLookupNames(ColorProfileLookupTestSuite* /*suite*/) {
    ASSERT_STREQ(getColorProfileName(-1, true), "HUNTER DEFAULT");
    ASSERT_STREQ(getColorProfileName(-1, false), "BOUNTY DEFAULT");
    ASSERT_STREQ(getColorProfileName(static_cast<int>(GameType::SIGNAL_ECHO), true), "SIGNAL ECHO");
}

// ============================================
// COLOR PROFILE PROMPT TEST SUITE
// ============================================

class ColorProfilePromptTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
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

    void skipToPrompt() {
        // Set up player state: pending profile game set
        device_.player->setPendingProfileGame(static_cast<int>(GameType::SIGNAL_ECHO));
        device_.game->skipToState(device_.pdn, 24);  // ColorProfilePrompt
        tick(1);
    }

    int getStateId() {
        return device_.game->getCurrentStateId();
    }

    DeviceInstance device_;
};

// Test: YES equips the color profile
void colorProfilePromptYesEquips(ColorProfilePromptTestSuite* suite) {
    suite->skipToPrompt();
    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);

    // Default selection is YES. Press secondary to confirm.
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
    ASSERT_EQ(suite->getStateId(), IDLE);
}

// Test: Toggling to NO then confirming does NOT equip
void colorProfilePromptNoDoesNotEquip(ColorProfilePromptTestSuite* suite) {
    suite->skipToPrompt();
    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);

    // Toggle to NO (primary), then confirm (secondary)
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), -1);
    ASSERT_EQ(suite->getStateId(), IDLE);
}

// Test: Auto-dismiss after timeout defaults to NO
void colorProfilePromptAutoDismiss(ColorProfilePromptTestSuite* suite) {
    suite->skipToPrompt();
    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);

    // Advance past 10s auto-dismiss
    suite->tickWithTime(20, 600);

    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), -1);
    ASSERT_EQ(suite->getStateId(), IDLE);
}

// Test: Display shows EQUIP text and YES/NO
void colorProfilePromptShowsDisplay(ColorProfilePromptTestSuite* suite) {
    suite->skipToPrompt();

    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundEquip = false;
    bool foundYesNo = false;
    for (const auto& entry : textHistory) {
        if (entry.find("EQUIP?") != std::string::npos) foundEquip = true;
        if (entry.find("YES") != std::string::npos) foundYesNo = true;
    }
    ASSERT_TRUE(foundEquip);
    ASSERT_TRUE(foundYesNo);
}

// Test: Clears pendingProfileGame on dismount
void colorProfilePromptClearsPending(ColorProfilePromptTestSuite* suite) {
    suite->skipToPrompt();

    suite->device_.player->setPendingProfileGame(7);
    // Confirm YES to transition away
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->device_.player->getPendingProfileGame(), -1);
}

// ============================================
// COLOR PROFILE PICKER TEST SUITE
// ============================================

class ColorProfilePickerTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    void skipToPicker() {
        // Add some eligible profiles
        device_.player->addColorProfileEligibility(static_cast<int>(GameType::SIGNAL_ECHO));
        device_.game->skipToState(device_.pdn, 25);  // ColorProfilePicker
        tick(1);
    }

    int getStateId() {
        return device_.game->getCurrentStateId();
    }

    DeviceInstance device_;
};

// Test: Picker shows COLOR PALETTE header
void colorProfilePickerShowsHeader(ColorProfilePickerTestSuite* suite) {
    suite->skipToPicker();
    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PICKER);

    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundHeader = false;
    for (const auto& entry : textHistory) {
        if (entry.find("COLOR PALETTE") != std::string::npos) foundHeader = true;
    }
    ASSERT_TRUE(foundHeader);
}

// Test: Scrolling wraps around profile list
void colorProfilePickerScrollWraps(ColorProfilePickerTestSuite* suite) {
    suite->skipToPicker();

    // profileList = [SIGNAL_ECHO(7), DEFAULT(-1)]
    // Pre-select: equipped=-1 → cursorIndex=1 (DEFAULT)
    // Cycle once → wraps to index 0 (SIGNAL_ECHO)
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Cycle again → back to index 1 (DEFAULT)
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Cycle third time → wraps to index 0 (SIGNAL_ECHO)
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Equip with PRIMARY — should equip SIGNAL_ECHO
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
}

// Test: Selecting DEFAULT equips -1
void colorProfilePickerSelectDefault(ColorProfilePickerTestSuite* suite) {
    suite->skipToPicker();

    // profileList = [SIGNAL_ECHO(7), DEFAULT(-1)]
    // Pre-select: equipped=-1 → cursorIndex=1 (DEFAULT)
    // Already on DEFAULT, just equip with PRIMARY
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(), -1);
    ASSERT_EQ(suite->getStateId(), IDLE);
}

// Test: Picker transitions back to Idle on confirm
void colorProfilePickerTransitionsToIdle(ColorProfilePickerTestSuite* suite) {
    suite->skipToPicker();

    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->getStateId(), IDLE);
}

// Test: Pre-selects currently equipped profile
void colorProfilePickerPreselectsEquipped(ColorProfilePickerTestSuite* suite) {
    // Equip Signal Echo before entering picker
    suite->device_.player->setEquippedColorProfile(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->skipToPicker();

    // Equip immediately with PRIMARY (should keep SIGNAL_ECHO since it was pre-selected)
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->device_.player->getEquippedColorProfile(),
              static_cast<int>(GameType::SIGNAL_ECHO));
}

// Test: Picker shows DEFAULT option when no palettes earned
void colorProfilePickerShowsDefaultOnly(ColorProfilePickerTestSuite* suite) {
    // No eligible profiles, just DEFAULT
    suite->device_.game->skipToState(suite->device_.pdn, 25);  // ColorProfilePicker
    suite->tick(1);

    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundDefault = false;
    for (const auto& entry : textHistory) {
        if (entry.find("DEFAULT") != std::string::npos) foundDefault = true;
    }
    ASSERT_TRUE(foundDefault);
}

// Test: Cycling profiles triggers LED preview
void colorProfilePickerLedPreview(ColorProfilePickerTestSuite* suite) {
    suite->skipToPicker();

    // Get initial LED state for left lights (first LED)
    auto initialColor = suite->device_.lightDriver->getLight(LightIdentifier::LEFT_LIGHTS, 0);

    // Cycle to next profile (SECONDARY button cycles, PRIMARY confirms)
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    // LED state should have changed (preview triggered)
    auto newColor = suite->device_.lightDriver->getLight(LightIdentifier::LEFT_LIGHTS, 0);
    bool changed = (initialColor.color.red != newColor.color.red ||
                    initialColor.color.green != newColor.color.green ||
                    initialColor.color.blue != newColor.color.blue);
    ASSERT_TRUE(changed);
}

// Test: Visual redesign shows updated control labels
void colorProfilePickerVisualRedesign(ColorProfilePickerTestSuite* suite) {
    suite->skipToPicker();

    // Check for new control text format
    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundNewFormat = false;
    for (const auto& entry : textHistory) {
        if (entry.find("[UP]") != std::string::npos ||
            entry.find("[DOWN]") != std::string::npos ||
            entry.find("CYCLE") != std::string::npos ||
            entry.find("SELECT") != std::string::npos) {
            foundNewFormat = true;
            break;
        }
    }
    ASSERT_TRUE(foundNewFormat);
}

// Test: Control label shows new format
void colorProfilePickerControlLabel(ColorProfilePickerTestSuite* suite) {
    suite->skipToPicker();

    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundNewControls = false;
    for (const auto& entry : textHistory) {
        if (entry.find("[UP] CYCLE") != std::string::npos ||
            entry.find("[DOWN] SELECT") != std::string::npos) {
            foundNewControls = true;
        }
    }
    ASSERT_TRUE(foundNewControls);
}

// ============================================
// FDN COMPLETE → COLOR PROMPT ROUTING
// ============================================

class FdnCompleteRoutingTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
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

    void setupWinOutcome(bool hardMode = false) {
        auto* echo = dynamic_cast<MiniGame*>(
            device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
        MiniGameOutcome outcome;
        outcome.result = MiniGameResult::WON;
        outcome.score = 100;
        outcome.hardMode = hardMode;
        echo->setOutcome(outcome);
    }

    int getStateId() {
        return device_.game->getCurrentStateId();
    }

    DeviceInstance device_;
};

// Test: Hard win routes to ColorProfilePrompt
void fdnCompleteRoutesToPromptOnHardWin(FdnCompleteRoutingTestSuite* suite) {
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->setupWinOutcome(true);  // hard mode
    suite->device_.player->setPendingChallenge("fdn:7:6");

    suite->device_.game->skipToState(suite->device_.pdn, 23);  // FdnComplete
    suite->tick(1);

    // pendingProfileGame should be set
    ASSERT_GE(suite->device_.player->getPendingProfileGame(), 0);

    // Wait for display timer
    suite->tickWithTime(10, 400);

    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);
}

// Test: Easy win routes to Idle (no color prompt)
void fdnCompleteRoutesToIdleOnEasyWin(FdnCompleteRoutingTestSuite* suite) {
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->setupWinOutcome(false);  // easy mode
    suite->device_.player->setPendingChallenge("fdn:7:6");

    suite->device_.game->skipToState(suite->device_.pdn, 23);  // FdnComplete
    suite->tick(1);

    // pendingProfileGame should NOT be set
    ASSERT_EQ(suite->device_.player->getPendingProfileGame(), -1);

    // Wait for display timer
    suite->tickWithTime(10, 400);

    ASSERT_EQ(suite->getStateId(), IDLE);
}

// Test: Loss routes to Idle (no color prompt)
void fdnCompleteRoutesToIdleOnLoss(FdnCompleteRoutingTestSuite* suite) {
    suite->device_.player->setLastFdnGameType(static_cast<int>(GameType::SIGNAL_ECHO));

    auto* echo = dynamic_cast<MiniGame*>(
        suite->device_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::LOST;
    outcome.score = 50;
    outcome.hardMode = true;
    echo->setOutcome(outcome);

    suite->device_.player->setPendingChallenge("fdn:7:6");

    suite->device_.game->skipToState(suite->device_.pdn, 23);  // FdnComplete
    suite->tick(1);

    suite->tickWithTime(10, 400);

    ASSERT_EQ(suite->getStateId(), IDLE);
}

// ============================================
// IDLE COLOR PICKER ENTRY TEST SUITE
// ============================================

class IdleColorPickerTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    void skipToIdle() {
        device_.game->skipToState(device_.pdn, 6);  // Idle
        tick(1);
    }

    int getStateId() {
        return device_.game->getCurrentStateId();
    }

    DeviceInstance device_;
};

// Test: Long press secondary enters ColorProfilePicker after Konami code (hard mode unlocked)
void idleLongPressEntersPicker(IdleColorPickerTestSuite* suite) {
    suite->device_.player->unlockHardMode();
    suite->skipToIdle();
    ASSERT_EQ(suite->getStateId(), IDLE);

    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
    suite->tick(2);

    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PICKER);
}

// Test: Long press does NOT enter picker before Konami code (hard mode not unlocked)
void idleLongPressNoPickerWithoutEligibility(IdleColorPickerTestSuite* suite) {
    suite->skipToIdle();
    ASSERT_EQ(suite->getStateId(), IDLE);

    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
    suite->tick(2);

    // Should stay in Idle since hard mode not unlocked
    ASSERT_EQ(suite->getStateId(), IDLE);
}

// ============================================
// STATE NAME MAPPING TESTS
// ============================================

class StateNameTestSuite : public testing::Test {};

void stateNameColorProfilePrompt(StateNameTestSuite* /*suite*/) {
    ASSERT_STREQ(getQuickdrawStateName(23), "ColorProfilePrompt");
}

void stateNameColorProfilePicker(StateNameTestSuite* /*suite*/) {
    ASSERT_STREQ(getQuickdrawStateName(24), "ColorProfilePicker");
}

#endif // NATIVE_BUILD
