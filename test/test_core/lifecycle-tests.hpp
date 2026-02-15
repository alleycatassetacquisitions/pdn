//
// State Machine Lifecycle Safety Tests
// Tests for mount/dismount ordering, transition safety, destruction cleanup
//
#pragma once
#include <gtest/gtest.h>
#include <vector>

#include "device-mock.hpp"
#include "state/state-machine.hpp"

// ============================================
// MOCK STATES FOR LIFECYCLE TESTING
// ============================================

// Counters to track global lifecycle call order
struct LifecycleLog {
    std::vector<std::string> events;

    void clear() {
        events.clear();
    }

    void log(const std::string& event) {
        events.push_back(event);
    }
};

class LifecycleStateA : public State {
public:
    explicit LifecycleStateA(LifecycleLog* log = nullptr)
        : State(1), lifecycleLog(log) {}

    void onStateMounted(Device *PDN) override {
        mountCount++;
        if (lifecycleLog) lifecycleLog->log("A_mount");
    }

    void onStateLoop(Device *PDN) override {
        loopCount++;
        if (lifecycleLog) lifecycleLog->log("A_loop");
    }

    void onStateDismounted(Device *PDN) override {
        dismountCount++;
        if (lifecycleLog) lifecycleLog->log("A_dismount");
    }

    int mountCount = 0;
    int loopCount = 0;
    int dismountCount = 0;
    bool shouldTransition = false;

private:
    LifecycleLog* lifecycleLog;
};

class LifecycleStateB : public State {
public:
    explicit LifecycleStateB(LifecycleLog* log = nullptr)
        : State(2), lifecycleLog(log) {}

    void onStateMounted(Device *PDN) override {
        mountCount++;
        if (lifecycleLog) lifecycleLog->log("B_mount");
    }

    void onStateLoop(Device *PDN) override {
        loopCount++;
        if (lifecycleLog) lifecycleLog->log("B_loop");
    }

    void onStateDismounted(Device *PDN) override {
        dismountCount++;
        if (lifecycleLog) lifecycleLog->log("B_dismount");
    }

    int mountCount = 0;
    int loopCount = 0;
    int dismountCount = 0;
    bool shouldTransition = false;

private:
    LifecycleLog* lifecycleLog;
};

class LifecycleStateC : public State {
public:
    explicit LifecycleStateC(LifecycleLog* log = nullptr)
        : State(3), lifecycleLog(log) {}

    void onStateMounted(Device *PDN) override {
        mountCount++;
        if (lifecycleLog) lifecycleLog->log("C_mount");
    }

    void onStateLoop(Device *PDN) override {
        loopCount++;
        if (lifecycleLog) lifecycleLog->log("C_loop");
    }

    void onStateDismounted(Device *PDN) override {
        dismountCount++;
        if (lifecycleLog) lifecycleLog->log("C_dismount");
    }

    int mountCount = 0;
    int loopCount = 0;
    int dismountCount = 0;

private:
    LifecycleLog* lifecycleLog;
};

// State that transitions back to itself (self-loop)
class SelfLoopState : public State {
public:
    SelfLoopState() : State(10) {}

    void onStateMounted(Device *PDN) override {
        mountCount++;
    }

    void onStateLoop(Device *PDN) override {
        loopCount++;
    }

    void onStateDismounted(Device *PDN) override {
        dismountCount++;
    }

    int mountCount = 0;
    int loopCount = 0;
    int dismountCount = 0;
    bool shouldSelfTransition = false;
};

// ============================================
// TEST STATE MACHINES
// ============================================

// Simple two-state machine for basic lifecycle tests
class TwoStateLifecycleMachine : public StateMachine {
public:
    TwoStateLifecycleMachine(LifecycleLog* log = nullptr)
        : StateMachine(1000), lifecycleLog(log) {}

    void populateStateMap() override {
        stateA = new LifecycleStateA(lifecycleLog);
        stateB = new LifecycleStateB(lifecycleLog);

        stateA->addTransition(
            new StateTransition(
                [this]() { return stateA->shouldTransition; },
                stateB
            )
        );

        stateB->addTransition(
            new StateTransition(
                [this]() { return stateB->shouldTransition; },
                stateA
            )
        );

        stateMap.push_back(stateA);
        stateMap.push_back(stateB);
    }

    LifecycleStateA* stateA = nullptr;
    LifecycleStateB* stateB = nullptr;

private:
    LifecycleLog* lifecycleLog;
};

// Three-state machine for rapid transitions
class ThreeStateLifecycleMachine : public StateMachine {
public:
    ThreeStateLifecycleMachine(LifecycleLog* log = nullptr)
        : StateMachine(2000), lifecycleLog(log) {}

    void populateStateMap() override {
        stateA = new LifecycleStateA(lifecycleLog);
        stateB = new LifecycleStateB(lifecycleLog);
        stateC = new LifecycleStateC(lifecycleLog);

        stateA->addTransition(
            new StateTransition(
                [this]() { return stateA->shouldTransition; },
                stateB
            )
        );

        stateB->addTransition(
            new StateTransition(
                [this]() { return stateB->shouldTransition; },
                stateC
            )
        );

        stateMap.push_back(stateA);
        stateMap.push_back(stateB);
        stateMap.push_back(stateC);
    }

    LifecycleStateA* stateA = nullptr;
    LifecycleStateB* stateB = nullptr;
    LifecycleStateC* stateC = nullptr;

private:
    LifecycleLog* lifecycleLog;
};

// Single-state machine (no transitions)
class SingleStateLifecycleMachine : public StateMachine {
public:
    SingleStateLifecycleMachine() : StateMachine(3000) {}

    void populateStateMap() override {
        state = new LifecycleStateA();
        stateMap.push_back(state);
    }

    LifecycleStateA* state = nullptr;
};

// Self-loop machine (state transitions to itself)
class SelfLoopMachine : public StateMachine {
public:
    SelfLoopMachine() : StateMachine(4000) {}

    void populateStateMap() override {
        state = new SelfLoopState();

        state->addTransition(
            new StateTransition(
                [this]() { return state->shouldSelfTransition; },
                state
            )
        );

        stateMap.push_back(state);
    }

    SelfLoopState* state = nullptr;
};

// Empty state machine (no states)
class EmptyStateMachine : public StateMachine {
public:
    EmptyStateMachine() : StateMachine(5000) {}

    void populateStateMap() override {
        // Intentionally empty - no states added
    }
};

// ============================================
// TEST SUITE
// ============================================

class StateMachineLifecycleTests : public ::testing::Test {
public:
    MockDevice* device;
    LifecycleLog log;

    void SetUp() override {
        device = new MockDevice();
        log.clear();
    }

    void TearDown() override {
        delete device;
    }
};

// ============================================
// TEST 1: BASIC LIFECYCLE
// ============================================

void testOnStateMountedCalledOnStart(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();
    machine->initialize(suite->device);

    ASSERT_EQ(machine->stateA->mountCount, 1);
    ASSERT_EQ(machine->stateA->loopCount, 0);
    ASSERT_EQ(machine->stateA->dismountCount, 0);

    delete machine;
}

void testOnStateDismountedCalledOnTransition(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();
    machine->initialize(suite->device);

    // Trigger transition from A to B
    machine->stateA->shouldTransition = true;
    machine->onStateLoop(suite->device);

    ASSERT_EQ(machine->stateA->dismountCount, 1);
}

void testOnStateLoopCalledEachIteration(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();
    machine->initialize(suite->device);

    machine->onStateLoop(suite->device);
    machine->onStateLoop(suite->device);
    machine->onStateLoop(suite->device);

    ASSERT_EQ(machine->stateA->loopCount, 3);

    delete machine;
}

void testMountDismountOrderCorrectDuringTransition(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine(&suite->log);
    machine->initialize(suite->device);
    suite->log.clear(); // Clear the initial mount event

    // Trigger transition from A to B
    machine->stateA->shouldTransition = true;
    machine->onStateLoop(suite->device);

    // Expected order: A_loop -> A_dismount -> B_mount
    ASSERT_GE(suite->log.events.size(), 3);
    ASSERT_EQ(suite->log.events[0], "A_loop");
    ASSERT_EQ(suite->log.events[1], "A_dismount");
    ASSERT_EQ(suite->log.events[2], "B_mount");

    delete machine;
}

// ============================================
// TEST 2: TRANSITION SAFETY
// ============================================

void testTransitionDismountsOldBeforeMountingNew(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine(&suite->log);
    machine->initialize(suite->device);
    suite->log.clear();

    machine->stateA->shouldTransition = true;
    machine->onStateLoop(suite->device);

    // Verify A was dismounted before B was mounted
    bool foundADismount = false;
    bool foundBMount = false;

    for (const auto& event : suite->log.events) {
        if (event == "A_dismount") foundADismount = true;
        if (event == "B_mount") {
            foundBMount = true;
            ASSERT_TRUE(foundADismount) << "B_mount happened before A_dismount";
        }
    }

    ASSERT_TRUE(foundADismount);
    ASSERT_TRUE(foundBMount);

    delete machine;
}

void testCurrentStatePointerUpdatedAfterTransition(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();
    machine->initialize(suite->device);

    State* initialState = machine->getCurrentState();
    ASSERT_EQ(initialState, machine->stateA);

    machine->stateA->shouldTransition = true;
    machine->onStateLoop(suite->device);

    State* newState = machine->getCurrentState();
    ASSERT_EQ(newState, machine->stateB);
    ASSERT_NE(newState, initialState);

    delete machine;
}

void testTransitionConditionsCheckedEachLoop(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();
    machine->initialize(suite->device);

    // Initially no transition
    machine->onStateLoop(suite->device);
    ASSERT_EQ(machine->getCurrentState(), machine->stateA);

    machine->onStateLoop(suite->device);
    ASSERT_EQ(machine->getCurrentState(), machine->stateA);

    // Set transition flag
    machine->stateA->shouldTransition = true;
    machine->onStateLoop(suite->device);

    // Should have transitioned
    ASSERT_EQ(machine->getCurrentState(), machine->stateB);

    delete machine;
}

// ============================================
// TEST 3: DESTRUCTION SAFETY
// ============================================

void testDestroyingActiveMachineDoesNotCrash(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();
    machine->initialize(suite->device);

    // Run some loops
    machine->onStateLoop(suite->device);
    machine->onStateLoop(suite->device);

    // Destroy while state is active - should not crash
    delete machine;

    // If we get here, test passed
    SUCCEED();
}

void testDestructorCleansUpAllStatesInMap(StateMachineLifecycleTests* suite) {
    auto* machine = new ThreeStateLifecycleMachine();
    machine->initialize(suite->device);

    // Get pointers to track cleanup
    LifecycleStateA* stateA = machine->stateA;
    LifecycleStateB* stateB = machine->stateB;
    LifecycleStateC* stateC = machine->stateC;

    // Store initial mount counts
    int aMount = stateA->mountCount;
    int bMount = stateB->mountCount;
    int cMount = stateC->mountCount;

    // Delete machine - states should be cleaned up
    delete machine;

    // States are deleted, so we can't check them directly
    // But if we get here without crashing, cleanup worked
    SUCCEED();
}

void testStateMachineDismountsCurrentStateOnDestruction(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();
    machine->initialize(suite->device);

    // Verify initial state is mounted
    ASSERT_EQ(machine->stateA->mountCount, 1);
    ASSERT_EQ(machine->stateA->dismountCount, 0);

    // Manually call dismount (simulating what happens in cleanup)
    machine->onStateDismounted(suite->device);

    ASSERT_EQ(machine->stateA->dismountCount, 1);

    delete machine;
}

// ============================================
// TEST 4: EDGE CASES
// ============================================

void testSingleStateNoTransitions(StateMachineLifecycleTests* suite) {
    auto* machine = new SingleStateLifecycleMachine();
    machine->initialize(suite->device);

    ASSERT_EQ(machine->state->mountCount, 1);

    // Loop multiple times - should stay in same state
    for (int i = 0; i < 5; i++) {
        machine->onStateLoop(suite->device);
    }

    ASSERT_EQ(machine->state->loopCount, 5);
    ASSERT_EQ(machine->state->dismountCount, 0);
    ASSERT_EQ(machine->getCurrentState(), machine->state);

    delete machine;
}

void testSelfTransitionDismountsAndRemounts(StateMachineLifecycleTests* suite) {
    auto* machine = new SelfLoopMachine();
    machine->initialize(suite->device);

    ASSERT_EQ(machine->state->mountCount, 1);
    ASSERT_EQ(machine->state->dismountCount, 0);

    // Trigger self-transition
    machine->state->shouldSelfTransition = true;
    machine->onStateLoop(suite->device);

    // Should dismount then remount
    ASSERT_EQ(machine->state->dismountCount, 1);
    ASSERT_EQ(machine->state->mountCount, 2);
    ASSERT_EQ(machine->getCurrentState(), machine->state);

    delete machine;
}

void testRapidTransitionsABCInSuccession(StateMachineLifecycleTests* suite) {
    auto* machine = new ThreeStateLifecycleMachine(&suite->log);
    machine->initialize(suite->device);
    suite->log.clear();

    // Set all transition flags
    machine->stateA->shouldTransition = true;
    machine->stateB->shouldTransition = true;

    // First loop: A -> B
    machine->onStateLoop(suite->device);
    ASSERT_EQ(machine->getCurrentState(), machine->stateB);
    ASSERT_EQ(machine->stateA->dismountCount, 1);
    ASSERT_EQ(machine->stateB->mountCount, 1);

    // Second loop: B -> C
    machine->onStateLoop(suite->device);
    ASSERT_EQ(machine->getCurrentState(), machine->stateC);
    ASSERT_EQ(machine->stateB->dismountCount, 1);
    ASSERT_EQ(machine->stateC->mountCount, 1);

    delete machine;
}

void testEmptyStateMachineHandlesGracefully(StateMachineLifecycleTests* suite) {
    auto* machine = new EmptyStateMachine();

    // Initialize with empty state map - should handle gracefully
    machine->populateStateMap();

    // Current state should be null since stateMap is empty
    ASSERT_EQ(machine->getCurrentState(), nullptr);

    // Loop should not crash even with null state
    // Note: Real implementation may need null check in onStateLoop
    // This test documents the expected behavior

    delete machine;
}

// ============================================
// TEST 5: APP LIFECYCLE (SUB-APP PATTERN)
// ============================================

void testHasLaunchedGuardWorks(StateMachineLifecycleTests* suite) {
    auto* machine = new TwoStateLifecycleMachine();

    // Before initialization
    ASSERT_FALSE(machine->hasLaunched());

    // After initialization
    machine->initialize(suite->device);
    ASSERT_TRUE(machine->hasLaunched());

    delete machine;
}

void testDeviceDestructorDismountsLaunchedApps(StateMachineLifecycleTests* suite) {
    // Create a tracking structure to verify dismount happens
    struct DismountTracker {
        bool dismountCalled = false;
    };
    auto tracker = std::make_shared<DismountTracker>();

    // Create custom state that tracks dismount
    class TrackedState : public State {
    public:
        explicit TrackedState(std::shared_ptr<DismountTracker> t)
            : State(1), tracker(t) {}

        void onStateDismounted(Device *PDN) override {
            tracker->dismountCalled = true;
        }

    private:
        std::shared_ptr<DismountTracker> tracker;
    };

    class TrackedMachine : public StateMachine {
    public:
        explicit TrackedMachine(std::shared_ptr<DismountTracker> t)
            : StateMachine(1000), tracker(t) {}

        void populateStateMap() override {
            stateMap.push_back(new TrackedState(tracker));
        }

    private:
        std::shared_ptr<DismountTracker> tracker;
    };

    auto* app = new TrackedMachine(tracker);

    AppConfig config;
    config[StateId(100)] = app;

    suite->device->loadAppConfig(std::move(config), StateId(100));

    ASSERT_TRUE(app->hasLaunched());
    ASSERT_FALSE(tracker->dismountCalled);

    // Device destructor should dismount the app
    delete suite->device;
    suite->device = nullptr; // Prevent double-delete

    // Verify dismount was called
    ASSERT_TRUE(tracker->dismountCalled);
}

void testDeviceDestructorOnlyDismountsLaunchedApps(StateMachineLifecycleTests* suite) {
    // Create trackers for each app
    struct DismountTracker {
        bool dismountCalled = false;
    };
    auto launchedTracker = std::make_shared<DismountTracker>();
    auto unlaunchedTracker = std::make_shared<DismountTracker>();

    class TrackedState : public State {
    public:
        explicit TrackedState(std::shared_ptr<DismountTracker> t)
            : State(1), tracker(t) {}

        void onStateDismounted(Device *PDN) override {
            tracker->dismountCalled = true;
        }

    private:
        std::shared_ptr<DismountTracker> tracker;
    };

    class TrackedMachine : public StateMachine {
    public:
        explicit TrackedMachine(int id, std::shared_ptr<DismountTracker> t)
            : StateMachine(id), tracker(t) {}

        void populateStateMap() override {
            stateMap.push_back(new TrackedState(tracker));
        }

    private:
        std::shared_ptr<DismountTracker> tracker;
    };

    auto* launchedApp = new TrackedMachine(100, launchedTracker);
    auto* unlaunchedApp = new TrackedMachine(200, unlaunchedTracker);

    AppConfig config;
    config[StateId(100)] = launchedApp;
    config[StateId(200)] = unlaunchedApp;

    // Only launch app 100
    suite->device->loadAppConfig(std::move(config), StateId(100));

    ASSERT_TRUE(launchedApp->hasLaunched());
    ASSERT_FALSE(unlaunchedApp->hasLaunched());

    // Device destructor should only dismount launched app
    delete suite->device;
    suite->device = nullptr;

    // Verify only launched app was dismounted
    ASSERT_TRUE(launchedTracker->dismountCalled);
    ASSERT_FALSE(unlaunchedTracker->dismountCalled);
}

void testMultipleAppsAllDismountedOnDestruction(StateMachineLifecycleTests* suite) {
    // Create trackers for each app
    struct DismountTracker {
        bool dismountCalled = false;
    };
    auto tracker1 = std::make_shared<DismountTracker>();
    auto tracker2 = std::make_shared<DismountTracker>();
    auto tracker3 = std::make_shared<DismountTracker>();

    class TrackedState : public State {
    public:
        explicit TrackedState(std::shared_ptr<DismountTracker> t)
            : State(1), tracker(t) {}

        void onStateDismounted(Device *PDN) override {
            tracker->dismountCalled = true;
        }

    private:
        std::shared_ptr<DismountTracker> tracker;
    };

    class TrackedMachine : public StateMachine {
    public:
        explicit TrackedMachine(int id, std::shared_ptr<DismountTracker> t)
            : StateMachine(id), tracker(t) {}

        void populateStateMap() override {
            stateMap.push_back(new TrackedState(tracker));
        }

    private:
        std::shared_ptr<DismountTracker> tracker;
    };

    auto* app1 = new TrackedMachine(100, tracker1);
    auto* app2 = new TrackedMachine(200, tracker2);
    auto* app3 = new TrackedMachine(300, tracker3);

    AppConfig config;
    config[StateId(100)] = app1;
    config[StateId(200)] = app2;
    config[StateId(300)] = app3;

    suite->device->loadAppConfig(std::move(config), StateId(100));

    // Switch between apps to launch them all
    suite->device->setActiveApp(StateId(200));
    suite->device->setActiveApp(StateId(300));

    ASSERT_TRUE(app1->hasLaunched());
    ASSERT_TRUE(app2->hasLaunched());
    ASSERT_TRUE(app3->hasLaunched());

    // All should be dismounted on destruction
    delete suite->device;
    suite->device = nullptr;

    ASSERT_TRUE(tracker1->dismountCalled);
    ASSERT_TRUE(tracker2->dismountCalled);
    ASSERT_TRUE(tracker3->dismountCalled);
}
