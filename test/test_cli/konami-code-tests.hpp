#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "game/konami-states/konami-code-entry.hpp"
#include "game/konami-states/konami-code-result.hpp"
#include "game/player.hpp"
#include "game/progress-manager.hpp"
#include "device/device-types.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// KONAMI CODE TEST SUITE
// ============================================

class KonamiCodeTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
        player_ = device_.player;
        quickdraw_ = dynamic_cast<Quickdraw*>(device_.game);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
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

    void advanceToIdle() {
        quickdraw_->skipToState(device_.pdn, 7);
        device_.pdn->loop();
    }

    void unlockAllKonamiButtons() {
        for (uint8_t i = 0; i < 7; i++) {
            player_->unlockKonamiButton(i);
        }
    }

    void simulatePrimaryButtonClick() {
        auto* btn = dynamic_cast<NativeButtonDriver*>(device_.pdn->getPrimaryButton());
        if (btn) btn->execCallback(ButtonInteraction::CLICK);
    }

    void simulateSecondaryButtonClick() {
        auto* btn = dynamic_cast<NativeButtonDriver*>(device_.pdn->getSecondaryButton());
        if (btn) btn->execCallback(ButtonInteraction::CLICK);
    }

    DeviceInstance device_;
    Player* player_ = nullptr;
    Quickdraw* quickdraw_ = nullptr;
};

// ============================================
// TEST FUNCTIONS
// ============================================

void testKonamiCodeCorrectSequence(KonamiCodeTestSuite* suite) {
    // Unlock all 7 Konami buttons
    suite->unlockAllKonamiButtons();

    // Jump to KonamiCodeEntry state (index 28)
    suite->quickdraw_->skipToState(suite->device_.pdn, 28);
    suite->tick();

    // Verify we're in the right state
    ASSERT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), KONAMI_CODE_ENTRY);

    // The correct sequence: UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A B A START
    // Button indices: 0 0 1 1 2 3 2 3 4 5 4 5 6
    const int sequence[] = {0, 0, 1, 1, 2, 3, 2, 3, 4, 5, 4, 5, 6};
    const int sequenceLength = 13;

    for (int i = 0; i < sequenceLength; i++) {
        int targetIndex = sequence[i];

        // We need to cycle the primary button until we reach the target button
        // Since buttons are in order 0-6, we can just click primary 'targetIndex' times
        // But we need to account for where we are currently
        // For simplicity, let's assume we start at index 0 and cycle forward

        // Get current position (we start at 0 after each commit)
        // We need to find how many clicks to get to targetIndex
        for (int clicks = 0; clicks < targetIndex; clicks++) {
            suite->simulatePrimaryButtonClick();
            suite->tick();
        }

        // Now commit with secondary button
        suite->simulateSecondaryButtonClick();
        suite->tick();
    }

    // After completing the sequence, we should transition to KonamiCodeAccepted
    suite->tick(5);
    EXPECT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), KONAMI_CODE_ACCEPTED);

    // Verify hard mode was unlocked
    EXPECT_TRUE(suite->player_->hasHardModeUnlocked());
}

void testKonamiCodeIncorrectInput(KonamiCodeTestSuite* suite) {
    // Unlock all 7 Konami buttons
    suite->unlockAllKonamiButtons();

    // Jump to KonamiCodeEntry state (index 28)
    suite->quickdraw_->skipToState(suite->device_.pdn, 28);
    suite->tick();

    // Enter first 3 buttons correctly: UP UP DOWN
    suite->simulateSecondaryButtonClick();  // UP (0)
    suite->tick();
    suite->simulateSecondaryButtonClick();  // UP (0)
    suite->tick();
    suite->simulatePrimaryButtonClick();    // Move to DOWN (1)
    suite->tick();
    suite->simulateSecondaryButtonClick();  // DOWN (1)
    suite->tick();

    // Now enter incorrect button (should be DOWN, but we'll enter UP)
    // Move back to UP (0)
    for (int i = 0; i < 6; i++) {  // Cycle through 7 buttons to get back to 0
        suite->simulatePrimaryButtonClick();
        suite->tick();
    }
    suite->simulateSecondaryButtonClick();  // UP (0) - WRONG!
    suite->tick();

    // The state should still be KonamiCodeEntry, but position should reset to 0
    EXPECT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), KONAMI_CODE_ENTRY);
}

void testKonamiCodeTimeout(KonamiCodeTestSuite* suite) {
    // Unlock all 7 Konami buttons
    suite->unlockAllKonamiButtons();

    // Jump to KonamiCodeEntry state (index 28)
    suite->quickdraw_->skipToState(suite->device_.pdn, 28);
    suite->tick();

    // Wait for timeout (30 seconds)
    suite->tickWithTime(1, 30100);

    // Should transition to GameOverReturnIdle
    EXPECT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), GAME_OVER_RETURN_IDLE);
}

void testKonamiCodeRejectedNoButtons(KonamiCodeTestSuite* suite) {
    // Don't unlock any buttons (player hasn't beaten any FDNs)

    // Jump to KonamiCodeRejected state (index 30)
    suite->quickdraw_->skipToState(suite->device_.pdn, 30);
    suite->tick();

    // Verify we're in the right state
    ASSERT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), KONAMI_CODE_REJECTED);

    // Wait for display timeout (4 seconds)
    suite->tickWithTime(5, 1000);

    // Should transition back to Idle
    EXPECT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), IDLE);
}

void testKonamiCodeAcceptedStateTransition(KonamiCodeTestSuite* suite) {
    // Unlock all 7 Konami buttons
    suite->unlockAllKonamiButtons();

    // Jump to KonamiCodeAccepted state (index 29)
    suite->quickdraw_->skipToState(suite->device_.pdn, 29);
    suite->tick();

    // Verify we're in the right state
    ASSERT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), KONAMI_CODE_ACCEPTED);

    // Verify hard mode gets unlocked
    EXPECT_TRUE(suite->player_->hasHardModeUnlocked());

    // Wait for display timeout (5 seconds)
    suite->tickWithTime(6, 1000);

    // Should transition back to Idle
    EXPECT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), IDLE);
}

void testGameOverReturnIdleTransition(KonamiCodeTestSuite* suite) {
    // Jump to GameOverReturnIdle state (index 31)
    suite->quickdraw_->skipToState(suite->device_.pdn, 31);
    suite->tick();

    // Verify we're in the right state
    ASSERT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), GAME_OVER_RETURN_IDLE);

    // Wait for display timeout (2 seconds)
    suite->tickWithTime(3, 1000);

    // Should transition back to Idle
    EXPECT_EQ(suite->quickdraw_->getCurrentState()->getStateId(), IDLE);
}

void testHardModeUnlockPersistence(KonamiCodeTestSuite* suite) {
    // Unlock hard mode directly
    suite->player_->unlockHardMode();

    // Save and verify
    EXPECT_TRUE(suite->player_->hasHardModeUnlocked());

    // Check that bit 7 is set in konamiProgress
    uint8_t progress = suite->player_->getKonamiProgress();
    EXPECT_EQ(progress & (1 << 7), (1 << 7));
}

#endif  // NATIVE_BUILD
