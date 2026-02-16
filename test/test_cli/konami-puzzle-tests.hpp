#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"

using namespace cli;

// ============================================
// KONAMI PUZZLE TEST SUITE
// ============================================

class KonamiPuzzleTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createDevice(0, true);  // Create player device
        SimpleTimer::setPlatformClock(device_.clockDriver);
        player_ = device_.player;
        quickdraw_ = dynamic_cast<Quickdraw*>(device_.game);
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

    // Helper: Advance player to Idle state
    void advanceToIdle() {
        // stateMap index 7 = Idle (in populateStateMap order)
        quickdraw_->skipToState(device_.pdn, 6);
        device_.pdn->loop();
    }

    // Helper: Unlock all 7 Konami buttons
    void unlockAllButtons() {
        for (uint8_t i = 0; i < 7; i++) {
            player_->unlockKonamiButton(i);
        }
    }

    DeviceInstance device_;
    Player* player_ = nullptr;
    Quickdraw* quickdraw_ = nullptr;
};

// ============================================
// KONAMI PUZZLE STATE TESTS
// ============================================

// Test: Player with all 7 buttons routes to KonamiPuzzle
void konamiPuzzleRoutingWithAllButtons(KonamiPuzzleTestSuite* suite) {
    // Unlock all 7 buttons
    suite->unlockAllButtons();

    // Advance to Idle
    suite->advanceToIdle();

    // Simulate FDN broadcast
    suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);

    ASSERT_EQ(suite->quickdraw_->getCurrentStateId(), FDN_DETECTED);

    // Simulate handshake (send fack)
    suite->device_.serialOutDriver->injectInput("*fack\r");
    suite->tick(5);

    // Should transition to KonamiPuzzle (state 26)
    State* state = suite->quickdraw_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), KONAMI_PUZZLE);
}

// Test: Player with < 7 buttons does NOT route to KonamiPuzzle
void konamiPuzzleNoRoutingWithoutAllButtons(KonamiPuzzleTestSuite* suite) {
    // Unlock only 5 buttons
    for (uint8_t i = 0; i < 5; i++) {
        suite->player_->unlockKonamiButton(i);
    }

    // Advance to Idle
    suite->advanceToIdle();

    // Simulate first encounter with Signal Echo
    suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);

    ASSERT_EQ(suite->quickdraw_->getCurrentStateId(), FDN_DETECTED);

    // Simulate handshake
    suite->device_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Should NOT be in KonamiPuzzle (should launch Signal Echo instead)
    // After handshake with first encounter, device switches to Signal Echo app
    State* state = suite->quickdraw_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_NE(state->getStateId(), KONAMI_PUZZLE);
}

// Test: KonamiPuzzle state mounts successfully
void konamiPuzzleStateMounts(KonamiPuzzleTestSuite* suite) {
    suite->unlockAllButtons();
    suite->quickdraw_->skipToState(suite->device_.pdn, KONAMI_PUZZLE);
    suite->tick(1);

    State* state = suite->quickdraw_->getCurrentState();
    ASSERT_EQ(state->getStateId(), KONAMI_PUZZLE);
}

// Test: Correct Konami Code sequence completes puzzle
void konamiPuzzleCorrectSequenceWins(KonamiPuzzleTestSuite* suite) {
    suite->unlockAllButtons();
    suite->quickdraw_->skipToState(suite->device_.pdn, KONAMI_PUZZLE);
    suite->tick(1);

    // The Konami Code: UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A START
    // Button mapping:
    // UP=0, DOWN=1, LEFT=2, RIGHT=3, B=4, A=5, START=6

    // Helper: Cycle to a specific button index and stamp
    int currentIndex = 0;
    auto cycleAndStamp = [&](int targetIndex) {
        // Get current state ID (should still be KONAMI_PUZZLE)
        State* s = suite->quickdraw_->getCurrentState();
        if (s->getStateId() != KONAMI_PUZZLE) return;

        // Cycle DOWN button to reach the target button (wrapping around modulo 7)
        while (currentIndex != targetIndex) {
            suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            suite->tick(1);
            currentIndex = (currentIndex + 1) % 7;
        }

        // Stamp with PRIMARY (UP) button
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    };

    // Enter the sequence: UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A START
    cycleAndStamp(0);  // UP (already at 0, just stamp)
    cycleAndStamp(0);  // UP (already at 0, just stamp)
    cycleAndStamp(1);  // DOWN
    cycleAndStamp(1);  // DOWN (already at 1)
    cycleAndStamp(2);  // LEFT
    cycleAndStamp(3);  // RIGHT
    cycleAndStamp(2);  // LEFT (cycle back)
    cycleAndStamp(3);  // RIGHT
    cycleAndStamp(4);  // B
    cycleAndStamp(5);  // A
    cycleAndStamp(6);  // START

    // Wait for success display
    suite->tickWithTime(10, 400);

    // Check that Konami Boon was awarded
    ASSERT_TRUE(suite->player_->hasKonamiBoon());
}

// Test: Wrong input shows error and allows retry
void konamiPuzzleWrongInputShowsError(KonamiPuzzleTestSuite* suite) {
    suite->unlockAllButtons();
    suite->quickdraw_->skipToState(suite->device_.pdn, KONAMI_PUZZLE);
    suite->tick(1);

    // Enter wrong first input (should be UP=0, enter DOWN=1)
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Cycle to DOWN
    suite->tick(1);
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Stamp
    suite->tick(1);

    // Should still be in KONAMI_PUZZLE (error state)
    State* state = suite->quickdraw_->getCurrentState();
    ASSERT_EQ(state->getStateId(), KONAMI_PUZZLE);

    // Wait for error to clear
    suite->tickWithTime(10, 300);

    // Should still be in KONAMI_PUZZLE (ready for retry)
    state = suite->quickdraw_->getCurrentState();
    ASSERT_EQ(state->getStateId(), KONAMI_PUZZLE);
}

// Test: hasAllKonamiButtons returns true when all unlocked
void hasAllKonamiButtonsTrue(KonamiPuzzleTestSuite* suite) {
    suite->unlockAllButtons();
    ASSERT_TRUE(suite->player_->hasAllKonamiButtons());
}

// Test: hasAllKonamiButtons returns false when missing buttons
void hasAllKonamiButtonsFalse(KonamiPuzzleTestSuite* suite) {
    // Unlock only 6 buttons
    for (uint8_t i = 0; i < 6; i++) {
        suite->player_->unlockKonamiButton(i);
    }
    ASSERT_FALSE(suite->player_->hasAllKonamiButtons());
}

// Test: State name resolves correctly
void konamiPuzzleStateName(KonamiPuzzleTestSuite* suite) {
    const char* name = getQuickdrawStateName(KONAMI_PUZZLE);
    ASSERT_STREQ(name, "KonamiPuzzle");
}

#endif
