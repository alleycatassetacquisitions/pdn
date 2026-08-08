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
    /// Identifiable by id so a test can tell which slot a mount landed on.
    explicit MockState(int stateId = 999)
        : State(stateId) {}
    // Minimal implementation - just exists to satisfy StateMachine requirements
};

// Registers three identifiable states and counts how often it is asked to. An
// app is re-mounted on every swap back to it, so a repopulate shows up here as
// both a second call and a grown state map.
class IndexedStateMachine : public StateMachine {
public:
    /// Registers nothing until the first mount, like every real app.
    explicit IndexedStateMachine(int appId)
        : StateMachine(appId) {}

    /// Appends three states and records that it was asked to.
    void populateStateMap() override {
        populateCount++;
        for (int stateId = 0; stateId < 3; stateId++) {
            stateMap.push_back(new MockState(stateId));
        }
    }

    int populateCount = 0;
};

// A state with two live edges: a hand-off declared above a local one. Both
// conditions are flags the test raises, so the priority between the two kinds is
// observable.
class ForkingState : public State {
public:
    /// Both edges are inert until a test raises the matching flag.
    explicit ForkingState(int stateId)
        : State(stateId) {}

    bool takeAppEdge = false;
    bool takeLocalEdge = false;
};

// Boot state forks to a state in another app or to its own second state.
class ForkingStateMachine : public StateMachine {
public:
    /// The hand-off names `targetAppId` and the state in it to enter at.
    ForkingStateMachine(int appId, StateId targetAppId, StateId entryStateId)
        : StateMachine(appId)
        , targetAppId(targetAppId)
        , entryStateId(entryStateId) {}

    /// Boot state gets the hand-off first, then a local edge to its sibling.
    void populateStateMap() override {
        ForkingState* fork = new ForkingState(0);
        ForkingState* local = new ForkingState(1);
        fork->addAppTransition([fork]() { return fork->takeAppEdge; },
                               targetAppId, entryStateId);
        fork->addTransition(new StateTransition(
            [fork]() { return fork->takeLocalEdge; }, local));
        stateMap.push_back(fork);
        stateMap.push_back(local);
    }

    /// The boot state, so a test can raise its edge conditions.
    ForkingState* forkState() {
        return static_cast<ForkingState*>(getStateMap()[0]);
    }

private:
    StateId targetAppId;
    StateId entryStateId;
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
    
    // Expose protected members for testing
    bool hasLaunchedPublic() const { return launched; }
    
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
        delete appOne;
        delete appTwo;
        delete appThree;
        delete device;
    }
};

// Real StateMachines rather than MockStateMachine: these cover the base class's
// own mount path, which MockStateMachine deliberately guards against re-entry.
class AppSwapTestSuite : public ::testing::Test {
public:
    MockDevice* device;
    IndexedStateMachine* appOne;
    IndexedStateMachine* appTwo;
    ForkingStateMachine* appThree;

    /// Apps are built here but populate lazily on their first mount.
    void SetUp() override {
        device = new MockDevice();
        appOne = new IndexedStateMachine(APP_ONE.id);
        appTwo = new IndexedStateMachine(APP_TWO.id);
        appThree = new ForkingStateMachine(APP_THREE.id, APP_TWO, StateId(2));
    }

    /// Each app frees the states it registered.
    void TearDown() override {
        delete appOne;
        delete appTwo;
        delete appThree;
        delete device;
    }

    /// Registers all three apps and boots into `launchAppId`.
    void loadAllApps(StateId launchAppId) {
        AppConfig config;
        config[APP_ONE] = appOne;
        config[APP_TWO] = appTwo;
        config[APP_THREE] = appThree;
        device->loadAppConfig(std::move(config), launchAppId);
    }
};
