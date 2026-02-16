//
// Created for testing Device app pattern
//
#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "device-mock.hpp"
#include "state/state-machine.hpp"

// Test app IDs - using StateId type for compatibility
const StateId APP_ONE(100);
const StateId APP_TWO(200);
const StateId APP_THREE(300);

// Simple mock state for MockStateMachine
class MockState : public State {
public:
    explicit MockState() : State(999) {}
    // Minimal implementation - just exists to satisfy StateMachine requirements
};

// Shared lifecycle counters that survive object deletion.
// Device::shutdownApps() deletes registered apps, so tests that verify
// destructor behavior need counters that outlive the mock objects.
struct LifecycleCounters {
    int mountedCount = 0;
    int loopCount = 0;
    int dismountedCount = 0;
    int pausedCount = 0;
    int resumedCount = 0;
    bool wasPaused = false;
};

// Mock StateMachine for testing
class MockStateMachine : public StateMachine {
public:
    // Shared counters survive deletion by Device::shutdownApps()
    std::shared_ptr<LifecycleCounters> counters;

    explicit MockStateMachine(int appId) : StateMachine(appId),
        counters(std::make_shared<LifecycleCounters>()) {
        // Initialize with a dummy state so currentState is not nullptr
        stateMap.push_back(new MockState());
    }

    void populateStateMap() override {
        // Already populated in constructor
    }

    void onStateMounted(Device *PDN) override {
        counters->mountedCount++;
        launched = true;
        // Call parent to properly initialize the state machine
        // This will call initialize() which sets currentState
        if (counters->mountedCount == 1) {
            StateMachine::onStateMounted(PDN);
        }
    }

    void onStateLoop(Device *PDN) override {
        counters->loopCount++;
    }

    void onStateDismounted(Device *PDN) override {
        counters->dismountedCount++;
    }

    std::unique_ptr<Snapshot> onStatePaused(Device *PDN) override {
        counters->pausedCount++;
        counters->wasPaused = true;
        // Call parent to set the base class's paused flag
        return StateMachine::onStatePaused(PDN);
    }

    void onStateResumed(Device *PDN, Snapshot* snapshot) override {
        counters->resumedCount++;
        counters->wasPaused = false;
        // Call parent to clear the base class's paused flag
        StateMachine::onStateResumed(PDN, snapshot);
    }

    // Expose protected members for testing
    bool hasLaunchedPublic() const { return launched; }
    bool isPausedPublic() const { return isPaused(); }

private:
    bool launched = false;
};

class DeviceTestSuite : public ::testing::Test {
public:
    MockDevice* device;
    MockStateMachine* appOne;
    MockStateMachine* appTwo;
    MockStateMachine* appThree;
    
    void SetUp() override {
        device = new MockDevice();
        appOne = new MockStateMachine(APP_ONE.id);
        appTwo = new MockStateMachine(APP_TWO.id);
        appThree = new MockStateMachine(APP_THREE.id);
    }
    
    void TearDown() override {
        // Note: appOne, appTwo, appThree are deleted by device destructor
        // since ownership was transferred via loadAppConfig()
        if (device != nullptr) {
            delete device;
        }
    }
};

// Regression test for #144: Device destructor must call onStateDismounted
// on all registered apps to ensure active state cleanup runs
void destructorDismountsActiveStateBeforeDeletion(DeviceTestSuite* suite) {
    // Capture shared counters before device takes ownership and deletes the mocks
    auto appOneCounters = suite->appOne->counters;

    AppConfig config;
    config[APP_ONE] = suite->appOne;

    suite->device->loadAppConfig(std::move(config), APP_ONE);

    // Run some loops to ensure state is active
    suite->device->loop();
    suite->device->loop();

    // Verify app is mounted and active
    ASSERT_EQ(appOneCounters->mountedCount, 1);
    ASSERT_EQ(appOneCounters->loopCount, 2);
    ASSERT_EQ(appOneCounters->dismountedCount, 0);

    // Delete device - this should call onStateDismounted on the active app
    // Note: shutdownApps() deletes the mock objects, so we use shared counters
    delete suite->device;
    suite->device = nullptr; // Prevent double-delete in TearDown

    // Verify dismount was called for the active app (via shared counter)
    ASSERT_EQ(appOneCounters->dismountedCount, 1);
}

// Verify that destructor dismounts ALL apps, not just the active one
void destructorDismountsAllRegisteredApps(DeviceTestSuite* suite) {
    // Capture shared counters before device takes ownership and deletes the mocks
    auto appOneCounters = suite->appOne->counters;
    auto appTwoCounters = suite->appTwo->counters;
    auto appThreeCounters = suite->appThree->counters;

    AppConfig config;
    config[APP_ONE] = suite->appOne;
    config[APP_TWO] = suite->appTwo;
    config[APP_THREE] = suite->appThree;

    suite->device->loadAppConfig(std::move(config), APP_ONE);

    // Switch between apps
    suite->device->setActiveApp(APP_TWO);
    suite->device->setActiveApp(APP_THREE);

    // Verify lifecycle before destruction
    ASSERT_EQ(appOneCounters->mountedCount, 1);
    ASSERT_EQ(appTwoCounters->mountedCount, 1);
    ASSERT_EQ(appThreeCounters->mountedCount, 1);

    // Delete device
    delete suite->device;
    suite->device = nullptr;

    // All apps should be dismounted (via shared counters)
    ASSERT_EQ(appOneCounters->dismountedCount, 1);
    ASSERT_EQ(appTwoCounters->dismountedCount, 1);
    ASSERT_EQ(appThreeCounters->dismountedCount, 1);
}

