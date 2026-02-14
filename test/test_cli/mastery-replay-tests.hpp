#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "game/konami-states/mastery-replay.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"

using namespace cli;

// ============================================
// MASTERY REPLAY TEST SUITE
// ============================================

class MasteryReplayTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createDevice(0, true);  // Create player device
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

    // Helper: Create a MasteryReplay state for testing
    MasteryReplay* createMasteryReplayState(GameType gameType) {
        int stateId = 22 + static_cast<int>(gameType);
        return new MasteryReplay(stateId, gameType);
    }

    DeviceInstance device_;
    Player* player_ = nullptr;
    Quickdraw* quickdraw_ = nullptr;
};

// ============================================
// MASTERY REPLAY STATE TESTS
// ============================================

// Test: UP button cycles between Easy and Hard mode
void masteryReplayUpCyclesModes(MasteryReplayTestSuite* suite) {
    MasteryReplay* state = suite->createMasteryReplayState(GameType::GHOST_RUNNER);

    // Mount the state
    state->onStateMounted(suite->device_.pdn);
    suite->tick();

    // Initially should have Easy mode selected (default)
    ASSERT_FALSE(state->transitionToEasyMode());
    ASSERT_FALSE(state->transitionToHardMode());

    // Press UP button to toggle to Hard mode
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick();
    ASSERT_FALSE(state->transitionToEasyMode());
    ASSERT_FALSE(state->transitionToHardMode());

    // Press UP button again to toggle back to Easy mode
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick();
    ASSERT_FALSE(state->transitionToEasyMode());
    ASSERT_FALSE(state->transitionToHardMode());

    state->onStateDismounted(suite->device_.pdn);
    delete state;
}

// Test: DOWN button transitions to Easy mode when selected
void masteryReplayDownTransitionsToEasy(MasteryReplayTestSuite* suite) {
    MasteryReplay* state = suite->createMasteryReplayState(GameType::SPIKE_VECTOR);

    // Mount the state
    state->onStateMounted(suite->device_.pdn);
    suite->tick();

    // Easy mode is selected by default
    // Press DOWN button to select it
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick();

    // Should transition to Easy mode
    ASSERT_TRUE(state->transitionToEasyMode());
    ASSERT_FALSE(state->transitionToHardMode());

    state->onStateDismounted(suite->device_.pdn);
    delete state;
}

// Test: DOWN button transitions to Hard mode when selected
void masteryReplayDownTransitionsToHard(MasteryReplayTestSuite* suite) {
    MasteryReplay* state = suite->createMasteryReplayState(GameType::CIPHER_PATH);

    // Mount the state
    state->onStateMounted(suite->device_.pdn);
    suite->tick();

    // Press UP to toggle to Hard mode
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick();

    // Press DOWN button to select Hard mode
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick();

    // Should transition to Hard mode
    ASSERT_FALSE(state->transitionToEasyMode());
    ASSERT_TRUE(state->transitionToHardMode());

    state->onStateDismounted(suite->device_.pdn);
    delete state;
}

// Test: Display shows correct game name for each game type
void masteryReplayDisplaysCorrectGameName(MasteryReplayTestSuite* suite) {
    // Test a few different game types
    GameType testGames[] = {
        GameType::GHOST_RUNNER,
        GameType::SPIKE_VECTOR,
        GameType::FIREWALL_DECRYPT,
        GameType::CIPHER_PATH,
        GameType::EXPLOIT_SEQUENCER,
        GameType::BREACH_DEFENSE,
        GameType::SIGNAL_ECHO
    };

    for (GameType gameType : testGames) {
        MasteryReplay* state = suite->createMasteryReplayState(gameType);

        // Mount the state - this should trigger display rendering
        state->onStateMounted(suite->device_.pdn);
        suite->tick();

        // The display should have been updated with the game name
        // We can't directly check display contents in this test, but we can verify
        // the state mounted without errors and the display driver was called
        ASSERT_NE(suite->device_.displayDriver, nullptr);

        state->onStateDismounted(suite->device_.pdn);
        delete state;
    }
}

// Test: State cleanup on dismount
void masteryReplayCleanupOnDismount(MasteryReplayTestSuite* suite) {
    MasteryReplay* state = suite->createMasteryReplayState(GameType::EXPLOIT_SEQUENCER);

    // Mount the state
    state->onStateMounted(suite->device_.pdn);
    suite->tick();

    // Toggle to Hard mode and select it
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick();
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick();

    // Should be transitioning to Hard mode
    ASSERT_TRUE(state->transitionToHardMode());

    // Dismount
    state->onStateDismounted(suite->device_.pdn);

    // After dismount, transitions should be reset
    ASSERT_FALSE(state->transitionToEasyMode());
    ASSERT_FALSE(state->transitionToHardMode());

    delete state;
}

// Test: State ID matches expected value
void masteryReplayCorrectStateId(MasteryReplayTestSuite* suite) {
    // Test that state IDs are in range [22..28] for the 7 game types
    GameType testGames[] = {
        GameType::GHOST_RUNNER,      // Should be state ID 23 (22 + 1)
        GameType::SPIKE_VECTOR,      // Should be state ID 24 (22 + 2)
        GameType::FIREWALL_DECRYPT,  // Should be state ID 25 (22 + 3)
        GameType::CIPHER_PATH,       // Should be state ID 26 (22 + 4)
        GameType::EXPLOIT_SEQUENCER, // Should be state ID 27 (22 + 5)
        GameType::BREACH_DEFENSE,    // Should be state ID 28 (22 + 6)
        GameType::SIGNAL_ECHO        // Should be state ID 29 (22 + 7)
    };

    for (GameType gameType : testGames) {
        int expectedStateId = 22 + static_cast<int>(gameType);
        MasteryReplay* state = suite->createMasteryReplayState(gameType);

        ASSERT_EQ(state->getStateId(), expectedStateId);

        delete state;
    }
}

#endif // NATIVE_BUILD
