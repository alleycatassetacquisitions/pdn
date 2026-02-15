#pragma once

#include <vector>
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "state.hpp"

/*
 * StateMachine can be thought of as the base class for "apps" on the PDN.
 * The class holds a stateMap, which is a vector of states, and moves through
 * those states based off of each state's StateTransitions.
 *
 * As a state machine moves through the stateMap, it invokes each state's lifecycle
 * methods in order.
 *
 * The basic control flow for a StateMachine looks like this:
 *
 * StateMachine initialized -> populates the state map, sets the current state and invokes
 * the first state's onStateMounted method.
 *
 * stateMachine.loop() must be invoked within the arduino loop function, which means
 * the statemachine loop will be invoked on every tick of the microcontroller.
 * (this hooks into the device loop())
 *
 *
 * From there, the current state's onStateLoop function is invoked.
 * At the end of each state's loop, state transitions are checked.
 *
 * If the condition for any of a state's transitions are met, the state machine
 * then invokes a transition to the new state.
 *
 * The current state is dismounted through a call to onStateDismounted.
 * Then, the current state is set to the state attached to the StateTransition.
 *
 * The new state's onStateMounted call is then invoked, at which point we return to
 * our loop function and continue with the new state's onStateLoop function.
*/

class StateMachine : public State {
public:
    explicit StateMachine(int stateId) : State(stateId) {}

    ~StateMachine() override {
        for (auto state: stateMap) {
            delete state;
        }
    };

    /**
     * Gracefully shutdown the state machine before destruction.
     * This sets a flag to prevent further state transitions, then calls the
     * virtual onStateDismounted() method to allow derived classes to perform cleanup.
     * Must be called before the destructor to avoid pure virtual method calls.
     */
    void shutdown(Device *PDN) {
        if (stopped) return;  // Already shutdown
        stopped = true;
        // Call the virtual onStateDismounted() to allow derived class overrides to execute.
        // The stopped flag prevents infinite recursion.
        onStateDismounted(PDN);
    }

    void initialize(Device *PDN) {
        populateStateMap();
        currentState = stateMap[0];
        currentState->onStateMounted(PDN);
        launched = true;
    }
    
    /**
     * Skip to a specific state by index, bypassing intermediate states.
     * Useful for testing scenarios where you want to start at a later state.
     * @param stateIndex The index in stateMap to skip to
     * @return true if successful, false if index out of range
     */
    bool skipToState(Device *PDN, int stateIndex) {
        if (stopped) return false;  // Don't transition if shutdown
        if (stateIndex < 0 || stateIndex >= static_cast<int>(stateMap.size())) {
            return false;
        }
        if (currentState) {
            currentState->onStateDismounted(PDN);
        }
        currentState = stateMap[stateIndex];
        currentState->onStateMounted(PDN);
        return true;
    }

    virtual void populateStateMap() = 0;

    void checkStateTransitions() {
        newState = currentState->checkTransitions();
        stateChangeReady = (newState != nullptr);

        #ifdef DEBUG
        // Debug assertion - validate state transitions during development
        if (stateChangeReady && newState == nullptr) {
            LOG_E("StateMachine", "ASSERTION FAILED: stateChangeReady true but newState is null");
        }
        #endif
    };

    void commitState(Device *PDN) {
        if (stopped) return;  // Don't transition if shutdown
        if (newState == nullptr) {
            LOG_W("StateMachine", "commitState called with null newState - skipping transition");
            stateChangeReady = false;
            return;
        }

        #ifdef DEBUG
        // Debug assertion - catch null transitions during development
        if (newState == nullptr) {
            LOG_E("StateMachine", "ASSERTION FAILED: commitState() received null state pointer");
        }
        #endif

        if (currentState) {
            currentState->onStateDismounted(PDN);
        }

        currentState = newState;
        stateChangeReady = false;
        newState = nullptr;

        if (currentState) {
            currentState->onStateMounted(PDN);
        }
    };

    State *getCurrentState() {
        return currentState;
    }

    void onStateMounted(Device *PDN) override {
        initialize(PDN);
    }

    /*
     * onStatePaused and onStateResume should be overridden in derived classes if the
     * state machine itself needs to hold onto any data beyond the current state's snapshot.
     */
    std::unique_ptr<Snapshot> onStatePaused(Device *PDN) override {
        if (stopped) return nullptr;  // Don't pause if shutdown
        if (currentState) {
            currentSnapshot = currentState->onStatePaused(PDN);
            currentState->onStateDismounted(PDN);
        }
        paused = true;
        return nullptr;
    }

    void onStateResumed(Device *PDN, Snapshot* stateMachineSnapshot) override {
        if (stopped) return;  // Don't resume if shutdown
        if (currentState) {
            currentState->onStateMounted(PDN);
            currentState->onStateResumed(PDN, currentSnapshot.get());
        }
        currentSnapshot = nullptr;
        paused = false;
    }

    void onStateLoop(Device *PDN) override {
        if (stopped) return;  // Don't process if shutdown
        if (currentState) {
            currentState->onStateLoop(PDN);
        }
        checkStateTransitions();
        if (stateChangeReady) {
            commitState(PDN);
        }
    }

    void onStateDismounted(Device *PDN) override {
        if (!stopped) {
            // Normal dismount (not during shutdown) - set the flag
            stopped = true;
        }
        // Perform cleanup: dismount the current state
        if (currentState) {
            currentState->onStateDismounted(PDN);
            currentState = nullptr;
        }
        newState = nullptr;
        stateChangeReady = false;
        currentSnapshot = nullptr;
    }

    bool hasLaunched() const {
        return launched;
    }

    bool isPaused() const {
        return paused;
    }

protected:
    // initial state is 0 in the list here
    std::vector<State *> stateMap;

    bool stateChangeReady = false;

    State *newState = nullptr;
    State *currentState = nullptr;

private:
    std::unique_ptr<Snapshot> currentSnapshot;

    bool launched = false;
    bool paused = false;
    bool stopped = false;  // Set to true when shutdown() is called
};
