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
        // Pre-populate stateMap so initialize() doesn't need to
        stateMap.push_back(new MockState());
    }
    
    void populateStateMap() override {
        // Already populated in constructor; guard prevents double-population
    }
    
    // Track lifecycle calls
    int mountedCount    = 0;
    int loopCount       = 0;
    int dismountedCount = 0;
    
    void onStateMounted(Device *PDN) override {
        mountedCount++;
        StateMachine::onStateMounted(PDN);
    }
    
    void onStateLoop(Device *PDN) override {
        loopCount++;
    }
    
    void onStateDismounted(Device *PDN) override {
        dismountedCount++;
        StateMachine::onStateDismounted(PDN);
    }

    bool hasLaunchedPublic() const { return hasLaunched(); }
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
        delete appOne;
        delete appTwo;
        delete appThree;
        delete device;
    }
};
