//
// Created for testing Device app pattern
//
#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "state/state-machine.hpp"

// Test app IDs - mapped to registered AppIds
const AppId APP_ONE  = AppId::PLAYER_REGISTRATION;
const AppId APP_TWO  = AppId::HANDSHAKE;
const AppId APP_THREE = AppId::QUICKDRAW;

// Simple mock state for MockStateMachine
class MockState : public State {
public:
    explicit MockState() : State(999) {}
    // Minimal implementation - just exists to satisfy StateMachine requirements
};

// Mock StateMachine for testing
class MockStateMachine : public StateMachine {
public:
    explicit MockStateMachine(AppId appId) : StateMachine(static_cast<int>(appId)) {
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
        appOne = new MockStateMachine(APP_ONE);
        appTwo = new MockStateMachine(APP_TWO);
        appThree = new MockStateMachine(APP_THREE);
    }
    
    void TearDown() override {
        delete appOne;
        delete appTwo;
        delete appThree;
        delete device;
    }
};

