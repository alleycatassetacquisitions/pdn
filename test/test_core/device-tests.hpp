//
// Created for testing Device app pattern
//
#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>

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

// Mock StateMachine for testing
class MockStateMachine : public StateMachine {
public:
    explicit MockStateMachine(int appId) : StateMachine(appId) {
        // Initialize with a dummy state so currentState is not nullptr
        stateMap.push_back(new MockState());
    }
    
    void populateStateMap() override {
        // Already populated in constructor
    }
    
    // Track lifecycle calls
    int mountedCount = 0;
    int loopCount = 0;
    int dismountedCount = 0;
    int pausedCount = 0;
    int resumedCount = 0;
    
    bool wasPaused = false;
    
    void onStateMounted(Device *PDN) override {
        mountedCount++;
        launched = true;
        // Call parent to properly initialize the state machine
        // This will call initialize() which sets currentState
        if (mountedCount == 1) {
            StateMachine::onStateMounted(PDN);
        }
    }
    
    void onStateLoop(Device *PDN) override {
        loopCount++;
    }
    
    void onStateDismounted(Device *PDN) override {
        dismountedCount++;
    }
    
    std::unique_ptr<Snapshot> onStatePaused(Device *PDN) override {
        pausedCount++;
        wasPaused = true;
        // Call parent to set the base class's paused flag
        return StateMachine::onStatePaused(PDN);
    }
    
    void onStateResumed(Device *PDN, Snapshot* snapshot) override {
        resumedCount++;
        wasPaused = false;
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
    AppConfig config;
    config[APP_ONE] = suite->appOne;

    suite->device->loadAppConfig(std::move(config), APP_ONE);

    // Run some loops to ensure state is active
    suite->device->loop();
    suite->device->loop();

    // Verify app is mounted and active
    ASSERT_EQ(suite->appOne->mountedCount, 1);
    ASSERT_EQ(suite->appOne->loopCount, 2);
    ASSERT_EQ(suite->appOne->dismountedCount, 0);

    // Delete device - this should call onStateDismounted on the active app
    delete suite->device;
    suite->device = nullptr; // Prevent double-delete in TearDown

    // Verify dismount was called for the active app
    ASSERT_EQ(suite->appOne->dismountedCount, 1);
}

// Verify that destructor dismounts ALL apps, not just the active one
void destructorDismountsAllRegisteredApps(DeviceTestSuite* suite) {
    AppConfig config;
    config[APP_ONE] = suite->appOne;
    config[APP_TWO] = suite->appTwo;
    config[APP_THREE] = suite->appThree;

    suite->device->loadAppConfig(std::move(config), APP_ONE);

    // Switch between apps
    suite->device->setActiveApp(APP_TWO);
    suite->device->setActiveApp(APP_THREE);

    // Verify lifecycle before destruction
    ASSERT_EQ(suite->appOne->mountedCount, 1);
    ASSERT_EQ(suite->appTwo->mountedCount, 1);
    ASSERT_EQ(suite->appThree->mountedCount, 1);

    // Delete device
    delete suite->device;
    suite->device = nullptr;

    // All apps should be dismounted
    ASSERT_EQ(suite->appOne->dismountedCount, 1);
    ASSERT_EQ(suite->appTwo->dismountedCount, 1);
    ASSERT_EQ(suite->appThree->dismountedCount, 1);
}

