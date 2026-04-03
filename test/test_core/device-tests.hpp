//
// Created for testing Device app pattern
//
#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "state/state-machine.hpp"

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
    
    int mountedCount = 0;
    int loopCount = 0;
    int dismountedCount = 0;
    
    void onStateMounted(Device *PDN) override {
        mountedCount++;
        launched = true;
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
    
    bool hasLaunchedPublic() const { return launched; }
    
private:
    bool launched = false;
};

class DeviceTestSuite : public ::testing::Test {
public:
    MockDevice* device;
    
    void SetUp() override {
        device = new MockDevice();
    }
    
    void TearDown() override {
        delete device;
    }
};

