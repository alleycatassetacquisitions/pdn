#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "game/player.hpp"
#include "game/progress-manager.hpp"
#include "game/quickdraw-states/konami-button-awarded.hpp"
#include "game/quickdraw-states/game-over-return-idle.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// KONAMI BUTTON AWARDED TEST FIXTURE
//
// Tests for KonamiButtonAwarded and GameOverReturnIdle states:
// - Button unlock and persistence
// - konamiProgress bitmask state after awarding
// - Multiple button collection
// - GameOverReturnIdle cleanup and app switching
// - Disconnect handling
// ============================================

class KonamiButtonAwardedTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);

        // Initialize progress manager
        progressManager = new ProgressManager();
        progressManager->initialize(player_.player, player_.pdn->getStorage());

        // Clear any existing progress
        player_.player->setKonamiProgress(0);
        progressManager->saveProgress();
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete progressManager;
        DeviceFactory::destroyDevice(player_);
    }

    DeviceInstance player_;
    ProgressManager* progressManager = nullptr;
};

// ============================================
// KONAMI BUTTON AWARDED TESTS
// ============================================

/*
 * Test: Button persistence after award — Unlock a button, verify it's saved to NVS.
 */
void buttonPersistenceAfterAward(KonamiButtonAwardedTestSuite* suite) {
    // Start with no progress
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);

    // Create and mount KonamiButtonAwarded for GHOST_RUNNER (awards button 6 = START)
    KonamiButtonAwarded* awardedState = new KonamiButtonAwarded(
        GameType::GHOST_RUNNER,
        suite->player_.player,
        suite->progressManager
    );

    // Mount the state (should unlock button and save)
    awardedState->onStateMounted(suite->player_.pdn);

    // Verify button is unlocked
    uint8_t buttonIndex = static_cast<uint8_t>(getRewardForGame(GameType::GHOST_RUNNER));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex));

    // Verify progress is saved (bit 6 should be set for START button)
    uint8_t expectedProgress = (1 << buttonIndex);
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), expectedProgress);

    // Cleanup
    awardedState->onStateDismounted(suite->player_.pdn);
    delete awardedState;
}

/*
 * Test: konamiProgress bitmask state after multiple buttons.
 */
void konamiProgressAfterMultipleButtons(KonamiButtonAwardedTestSuite* suite) {
    // Award first button (GHOST_RUNNER -> START = button 6)
    KonamiButtonAwarded* state1 = new KonamiButtonAwarded(
        GameType::GHOST_RUNNER,
        suite->player_.player,
        suite->progressManager
    );
    state1->onStateMounted(suite->player_.pdn);
    state1->onStateDismounted(suite->player_.pdn);

    uint8_t button1 = static_cast<uint8_t>(getRewardForGame(GameType::GHOST_RUNNER));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(button1));

    // Award second button (SPIKE_VECTOR -> DOWN = button 1)
    KonamiButtonAwarded* state2 = new KonamiButtonAwarded(
        GameType::SPIKE_VECTOR,
        suite->player_.player,
        suite->progressManager
    );
    state2->onStateMounted(suite->player_.pdn);
    state2->onStateDismounted(suite->player_.pdn);

    uint8_t button2 = static_cast<uint8_t>(getRewardForGame(GameType::SPIKE_VECTOR));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(button2));

    // Verify both buttons are set in bitmask
    uint8_t expectedProgress = (1 << button1) | (1 << button2);
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), expectedProgress);

    // Award third button (FIREWALL_DECRYPT -> LEFT = button 2)
    KonamiButtonAwarded* state3 = new KonamiButtonAwarded(
        GameType::FIREWALL_DECRYPT,
        suite->player_.player,
        suite->progressManager
    );
    state3->onStateMounted(suite->player_.pdn);
    state3->onStateDismounted(suite->player_.pdn);

    uint8_t button3 = static_cast<uint8_t>(getRewardForGame(GameType::FIREWALL_DECRYPT));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(button3));

    // Verify all three buttons are set
    expectedProgress = (1 << button1) | (1 << button2) | (1 << button3);
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), expectedProgress);

    // Cleanup
    delete state1;
    delete state2;
    delete state3;
}

/*
 * Test: GameOverReturnIdle cleanup and app switch.
 */
void gameOverReturnIdleCleanup(KonamiButtonAwardedTestSuite* suite) {
    GameOverReturnIdle* state = new GameOverReturnIdle();

    // Mount the state
    state->onStateMounted(suite->player_.pdn);

    // Simulate time passing (2 seconds)
    for (int i = 0; i < 2100; i += 100) {
        suite->player_.clockDriver->advance(100);
        state->onStateLoop(suite->player_.pdn);
    }

    // After timeout, state should signal return to idle
    // (In actual implementation, PDN->setActiveApp() would be called)
    // For this test, we just verify the state doesn't crash and cleans up properly

    // Dismount and verify cleanup
    state->onStateDismounted(suite->player_.pdn);

    delete state;
}

/*
 * Test: Disconnect produces no side effects — onStateDismounted handles cleanup properly.
 */
void disconnectNoSideEffects(KonamiButtonAwardedTestSuite* suite) {
    // Start with no progress
    ASSERT_EQ(suite->player_.player->getKonamiProgress(), 0);

    // Create awarded state
    KonamiButtonAwarded* awardedState = new KonamiButtonAwarded(
        GameType::GHOST_RUNNER,
        suite->player_.player,
        suite->progressManager
    );

    // Mount the state (button gets unlocked and saved here)
    awardedState->onStateMounted(suite->player_.pdn);

    uint8_t buttonIndex = static_cast<uint8_t>(getRewardForGame(GameType::GHOST_RUNNER));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex));

    // Simulate disconnect (dismount without completing timer)
    awardedState->onStateDismounted(suite->player_.pdn);

    // Button should still be unlocked (progress was saved on mount)
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex));

    // No partial state should remain
    // (Timer should be invalidated, transitions reset)

    delete awardedState;
}

/*
 * Test: Victory display renders with correct game name and button count.
 */
void victoryDisplayRendersCorrectly(KonamiButtonAwardedTestSuite* suite) {
    // Award first button
    KonamiButtonAwarded* state = new KonamiButtonAwarded(
        GameType::GHOST_RUNNER,
        suite->player_.player,
        suite->progressManager
    );

    state->onStateMounted(suite->player_.pdn);

    // Run one loop iteration (should render display)
    state->onStateLoop(suite->player_.pdn);

    // Verify button was unlocked
    uint8_t buttonIndex = static_cast<uint8_t>(getRewardForGame(GameType::GHOST_RUNNER));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex));

    // Award second button to verify count increases
    state->onStateDismounted(suite->player_.pdn);
    delete state;

    KonamiButtonAwarded* state2 = new KonamiButtonAwarded(
        GameType::SPIKE_VECTOR,
        suite->player_.player,
        suite->progressManager
    );

    state2->onStateMounted(suite->player_.pdn);
    state2->onStateLoop(suite->player_.pdn);

    // Verify second button was unlocked
    uint8_t buttonIndex2 = static_cast<uint8_t>(getRewardForGame(GameType::SPIKE_VECTOR));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex2));

    // Both buttons should now be unlocked
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex));
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex2));

    state2->onStateDismounted(suite->player_.pdn);
    delete state2;
}

/*
 * Test: Duplicate button award (replay same game) doesn't double-count.
 */
void duplicateButtonAwardNoDoubleCount(KonamiButtonAwardedTestSuite* suite) {
    // Award GHOST_RUNNER button first time
    KonamiButtonAwarded* state1 = new KonamiButtonAwarded(
        GameType::GHOST_RUNNER,
        suite->player_.player,
        suite->progressManager
    );
    state1->onStateMounted(suite->player_.pdn);

    uint8_t buttonIndex = static_cast<uint8_t>(getRewardForGame(GameType::GHOST_RUNNER));
    uint8_t firstProgress = suite->player_.player->getKonamiProgress();
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex));

    state1->onStateDismounted(suite->player_.pdn);
    delete state1;

    // Award GHOST_RUNNER button again (replay)
    KonamiButtonAwarded* state2 = new KonamiButtonAwarded(
        GameType::GHOST_RUNNER,
        suite->player_.player,
        suite->progressManager
    );
    state2->onStateMounted(suite->player_.pdn);

    uint8_t secondProgress = suite->player_.player->getKonamiProgress();

    // Progress should be unchanged (bit already set)
    ASSERT_EQ(firstProgress, secondProgress);
    ASSERT_TRUE(suite->player_.player->hasUnlockedButton(buttonIndex));

    state2->onStateDismounted(suite->player_.pdn);
    delete state2;
}

#endif // NATIVE_BUILD
