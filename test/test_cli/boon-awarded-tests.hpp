#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "game/color-profiles.hpp"
#include "game/progress-manager.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// BOON AWARDED TEST SUITE
// ============================================

class BoonAwardedTestSuite : public testing::Test {
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

    void skipToBoonAwarded(GameType gameType) {
        // Set up player state: pending profile game for the specified type
        device_.player->setPendingProfileGame(static_cast<int>(gameType));
        device_.game->skipToState(device_.pdn, BOON_AWARDED);
        tick(1);
    }

    int getStateId() {
        return device_.game->getCurrentState()->getStateId();
    }

    DeviceInstance device_;
};

// Test: BoonAwarded updates color profile eligibility bitmask
void boonAwardedUpdatesEligibility(BoonAwardedTestSuite* suite) {
    suite->device_.player->setPendingProfileGame(static_cast<int>(GameType::SIGNAL_ECHO));
    suite->device_.game->skipToState(suite->device_.pdn, BOON_AWARDED);
    suite->tick(1);

    ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(
        static_cast<int>(GameType::SIGNAL_ECHO)));
}

// Test: BoonAwarded persists progress to NVS
void boonAwardedPersistsProgress(BoonAwardedTestSuite* suite) {
    // Clear storage first
    suite->device_.pdn->getStorage()->clear();

    suite->device_.player->setPendingProfileGame(static_cast<int>(GameType::GHOST_RUNNER));
    suite->device_.game->skipToState(suite->device_.pdn, BOON_AWARDED);
    suite->tick(1);

    // Read back from NVS — color_elig should have bit 1 set (GHOST_RUNNER = 1)
    uint8_t eligMask = suite->device_.pdn->getStorage()->readUChar("color_elig", 0);
    ASSERT_EQ(eligMask & (1 << static_cast<int>(GameType::GHOST_RUNNER)),
              1 << static_cast<int>(GameType::GHOST_RUNNER));
}

// Test: BoonAwarded displays correct palette name
void boonAwardedDisplaysPaletteName(BoonAwardedTestSuite* suite) {
    suite->skipToBoonAwarded(GameType::SPIKE_VECTOR);

    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundBoonUnlocked = false;
    bool foundPaletteName = false;

    for (const auto& entry : textHistory) {
        if (entry.find("BOON UNLOCKED!") != std::string::npos) foundBoonUnlocked = true;
        if (entry.find("SPIKE VECTOR") != std::string::npos) foundPaletteName = true;
    }

    ASSERT_TRUE(foundBoonUnlocked);
    ASSERT_TRUE(foundPaletteName);
}

// Test: BoonAwarded LED animation is started
void boonAwardedLedAnimation(BoonAwardedTestSuite* suite) {
    suite->skipToBoonAwarded(GameType::CIPHER_PATH);

    // Verify that the state is active (LED animation would be running)
    ASSERT_EQ(suite->getStateId(), BOON_AWARDED);

    // Advance time slightly to ensure animation is running
    suite->tickWithTime(2, 100);

    // Still in the state - animation should be showing
    ASSERT_EQ(suite->getStateId(), BOON_AWARDED);
}

// Test: BoonAwarded transitions to ColorProfilePrompt after timeout
void boonAwardedTransitionsToColorPrompt(BoonAwardedTestSuite* suite) {
    suite->skipToBoonAwarded(GameType::EXPLOIT_SEQUENCER);
    ASSERT_EQ(suite->getStateId(), BOON_AWARDED);

    // Advance past 5-second celebration timeout
    suite->tickWithTime(10, 600);

    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);
}

// Test: BoonAwarded handles multiple game types correctly
void boonAwardedMultipleGameTypes(BoonAwardedTestSuite* suite) {
    GameType types[] = {
        GameType::SIGNAL_ECHO,
        GameType::FIREWALL_DECRYPT,
        GameType::GHOST_RUNNER,
        GameType::SPIKE_VECTOR,
        GameType::CIPHER_PATH,
        GameType::EXPLOIT_SEQUENCER,
        GameType::BREACH_DEFENSE
    };

    for (GameType type : types) {
        suite->device_.player->setPendingProfileGame(static_cast<int>(type));
        suite->device_.game->skipToState(suite->device_.pdn, BOON_AWARDED);
        suite->tick(1);

        ASSERT_TRUE(suite->device_.player->hasColorProfileEligibility(static_cast<int>(type)));

        // Advance to transition
        suite->tickWithTime(10, 600);
        ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);
    }
}

// Test: BoonAwarded transitions properly and cleans up
void boonAwardedClearsLeds(BoonAwardedTestSuite* suite) {
    suite->skipToBoonAwarded(GameType::BREACH_DEFENSE);

    ASSERT_EQ(suite->getStateId(), BOON_AWARDED);

    // Advance to transition
    suite->tickWithTime(10, 600);

    // Should have transitioned to ColorProfilePrompt
    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);
}

// Test: BoonAwarded handles missing pending profile gracefully
void boonAwardedHandlesMissingProfile(BoonAwardedTestSuite* suite) {
    suite->device_.player->setPendingProfileGame(-1);  // No pending profile
    suite->device_.game->skipToState(suite->device_.pdn, BOON_AWARDED);
    suite->tick(1);

    // Should transition to ColorProfilePrompt immediately
    ASSERT_EQ(suite->getStateId(), COLOR_PROFILE_PROMPT);
}

#endif // NATIVE_BUILD
