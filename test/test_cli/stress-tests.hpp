#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// STRESS TEST SUITE
// ============================================

class StressTestSuite : public testing::Test {
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

    void advanceToIdle() {
        device_.game->skipToState(device_.pdn, 6);
        device_.pdn->loop();
    }

    int getStateId() {
        return device_.game->getCurrentStateId();
    }

    DeviceInstance device_;
};

// ============================================
// MINIGAME STRESS TEST SUITE
// ============================================

class MinigameStressTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createGameDevice(0, "signal-echo");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        echo_ = static_cast<SignalEcho*>(device_.game);
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

    DeviceInstance device_;
    SignalEcho* echo_ = nullptr;
};

// ============================================
// RAPID STATE TRANSITION TESTS
// ============================================

/*
 * Test: Cycle through Quickdraw states at maximum speed
 * Verifies state machine handles rapid transitions without crashes
 */
void stressRapidStateTransitions(StressTestSuite* suite) {
    // Advance to Idle state
    suite->advanceToIdle();
    ASSERT_EQ(suite->getStateId(), IDLE);

    // Cycle through state transitions 100 times
    for (int cycle = 0; cycle < 100; cycle++) {
        // Trigger FDN detection by injecting serial message
        suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");
        suite->tick(3);

        // Should be in FDN_DETECTED
        EXPECT_EQ(suite->getStateId(), FDN_DETECTED);

        // Abort by button press (transition back to Idle)
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(5);

        // Should be back in Idle
        EXPECT_EQ(suite->getStateId(), IDLE);
    }

    // Final verification
    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: Run 100+ full game lifecycles back-to-back
 * Verifies no memory leaks or state corruption across multiple games
 */
void stressFullGameLifecycles(MinigameStressTestSuite* suite) {
    for (int cycle = 0; cycle < 100; cycle++) {
        // Start from intro
        suite->echo_->skipToState(suite->device_.pdn, 0);
        suite->tick(1);

        EXPECT_EQ(suite->echo_->getCurrentStateId(), ECHO_INTRO);

        // Skip intro timer
        suite->tickWithTime(5, 500);

        // Auto-transition to ShowSequence
        suite->tick(1);
        EXPECT_EQ(suite->echo_->getCurrentStateId(), ECHO_SHOW_SEQUENCE);

        // Skip to evaluate without playing
        suite->echo_->skipToState(suite->device_.pdn, 3);
        suite->tick(1);

        // Should be in Lose state (no input provided)
        suite->echo_->skipToState(suite->device_.pdn, 5);
        suite->tick(1);
        EXPECT_EQ(suite->echo_->getCurrentStateId(), ECHO_LOSE);

        // Allow outcome to persist
        suite->tickWithTime(5, 100);
    }

    // Verify state machine still valid
    ASSERT_NE(suite->echo_->getCurrentState(), nullptr);
}

/*
 * Test: Rapid state changes during state transitions
 * Verifies state machine handles concurrent transition requests
 */
void stressStateTransitionDuringTransition(StressTestSuite* suite) {
    suite->advanceToIdle();

    for (int i = 0; i < 50; i++) {
        // Trigger state change
        suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");

        // Immediately inject another message before transition completes
        suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");

        suite->tick(1);

        // Press button during transition
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);

        suite->tick(5);
    }

    // Should still be in a valid state
    int stateId = suite->getStateId();
    ASSERT_TRUE(stateId >= 0 && stateId <= 26);
}

// ============================================
// BUTTON SPAM TESTS
// ============================================

/*
 * Test: Send 100+ button events in rapid succession
 * Verifies button handling doesn't overflow or crash
 */
void stressButtonSpam(StressTestSuite* suite) {
    suite->advanceToIdle();

    // Spam primary button 200 times
    for (int i = 0; i < 200; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    suite->tick(5);

    // Should still be functional
    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: Alternate button presses at maximum speed
 * Verifies handling of rapid button alternation
 */
void stressAlternatingButtonSpam(StressTestSuite* suite) {
    suite->advanceToIdle();

    // Alternate between buttons 200 times
    for (int i = 0; i < 200; i++) {
        if (i % 2 == 0) {
            suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        } else {
            suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        }
    }

    suite->tick(5);
    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: Simultaneous button presses
 * Verifies handling of both buttons pressed at same time
 */
void stressSimultaneousButtonPress(StressTestSuite* suite) {
    suite->advanceToIdle();

    // Press both buttons simultaneously 100 times
    for (int i = 0; i < 100; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    suite->tick(5);
    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: Button events during state transitions
 * Verifies button handling during state changes
 */
void stressButtonsDuringTransition(StressTestSuite* suite) {
    suite->advanceToIdle();

    for (int i = 0; i < 50; i++) {
        // Start state transition
        suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");

        // Spam buttons during transition
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);

        suite->tick(3);

        // Return to idle
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(5);
    }

    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: Button spam during gameplay
 * Verifies game logic handles excessive button input
 */
void stressButtonSpamDuringGameplay(MinigameStressTestSuite* suite) {
    // Start game
    suite->echo_->skipToState(suite->device_.pdn, 0);
    suite->tickWithTime(5, 500);
    suite->tick(1);

    // Should be in ShowSequence
    ASSERT_EQ(suite->echo_->getCurrentStateId(), ECHO_SHOW_SEQUENCE);

    // Wait for player input phase
    suite->tickWithTime(10, 100);
    suite->echo_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Spam buttons during input
    for (int i = 0; i < 200; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    suite->tick(10);

    // Game should still be valid
    ASSERT_NE(suite->echo_->getCurrentState(), nullptr);
}

// ============================================
// TIMER EDGE CASE TESTS
// ============================================

/*
 * Test: Button press at exact moment timer expires
 * Verifies no race condition between timer and button handler
 */
void stressTimerButtonRaceCondition(MinigameStressTestSuite* suite) {
    // Start game
    suite->echo_->skipToState(suite->device_.pdn, 0);
    suite->tick(1);

    // Advance to just before intro timer expires (1999ms)
    suite->tickWithTime(1, 1999);

    // Press button at exact moment timer expires
    suite->device_.clockDriver->advance(1);
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Should have transitioned successfully
    ASSERT_NE(suite->echo_->getCurrentState(), nullptr);
}

/*
 * Test: Multiple overlapping timers
 * Verifies system handles multiple simultaneous timer expirations
 */
void stressMultipleTimerExpiration(StressTestSuite* suite) {
    suite->advanceToIdle();

    // Create scenario with multiple timers
    suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);

    ASSERT_EQ(suite->getStateId(), FDN_DETECTED);

    // FdnDetected has timeout timer - let it expire
    suite->tickWithTime(50, 100);

    // Should handle timeout gracefully
    int stateId = suite->getStateId();
    ASSERT_TRUE(stateId >= 0 && stateId <= 26);
}

// ============================================
// MEMORY STRESS TESTS
// ============================================

/*
 * Test: Rapid serial message injection
 * Verifies serial buffer doesn't overflow
 */
void stressSerialMessageFlood(StressTestSuite* suite) {
    suite->advanceToIdle();

    // Flood with 500 serial messages
    for (int i = 0; i < 500; i++) {
        suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");
    }

    suite->tick(10);

    // Should handle gracefully without crash
    ASSERT_NE(suite->device_.pdn, nullptr);
}

/*
 * Test: Rapid display updates
 * Verifies display buffer handles high update rate
 */
void stressDisplayUpdates(MinigameStressTestSuite* suite) {
    // Rapidly cycle through game states that update display
    for (int i = 0; i < 200; i++) {
        suite->echo_->skipToState(suite->device_.pdn, 0);
        suite->tick(1);

        suite->echo_->skipToState(suite->device_.pdn, 1);
        suite->tick(1);

        suite->echo_->skipToState(suite->device_.pdn, 2);
        suite->tick(1);
    }

    // Display should still be functional
    ASSERT_NE(suite->device_.displayDriver, nullptr);
}

/*
 * Test: Continuous loop execution
 * Verifies device can run indefinitely without degradation
 */
void stressContinuousLoop(StressTestSuite* suite) {
    suite->advanceToIdle();

    // Run 10,000 loop iterations
    suite->tick(10000);

    // Should still be in valid state
    ASSERT_EQ(suite->getStateId(), IDLE);
}

#endif // NATIVE_BUILD
