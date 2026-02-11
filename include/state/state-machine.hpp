#pragma once

#include <vector>
#include <memory>
#include "state.hpp"

class Device;

/*
 * StateMachine can be thought of as the base class for "apps" on the PDN.
 * The class holds a stateMap, which is a vector of states, and moves through
 * those states based off of each state's StateTransitions.
 *
 * StateMachine extends State, making it composable — a state machine can be
 * managed exactly like a state. This enables Device to manage multiple
 * state machines through its AppConfig.
 *
 * As a state machine moves through the stateMap, it invokes each state's lifecycle
 * methods in order.
 *
 * The basic control flow for a StateMachine looks like this:
 *
 * StateMachine initialized -> populates the state map, sets the current state and invokes
 * the first state's onStateMounted method.
 *
 * stateMachine.onStateLoop() must be invoked within the device loop function, which means
 * the statemachine loop will be invoked on every tick of the microcontroller.
 * (this hooks into the device loop())
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
    explicit StateMachine(int stateId) : State(stateId) {
    }

    virtual ~StateMachine() {
        for (auto state: stateMap) {
            delete state;
        }
    };

    void initialize(Device *PDN) {
        populateStateMap();
        currentState = stateMap[0];
        currentState->onStateMounted(PDN);
    }

    /**
     * Skip to a specific state by index, bypassing intermediate states.
     * Useful for testing scenarios where you want to start at a later state.
     * @param PDN The device pointer
     * @param stateIndex The index in stateMap to skip to
     * @return true if successful, false if index out of range
     */
    bool skipToState(Device *PDN, int stateIndex) {
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
    };

    void commitState(Device *PDN) {
        currentState->onStateDismounted(PDN);

        currentState = newState;
        stateChangeReady = false;
        newState = nullptr;

        currentState->onStateMounted(PDN);
    };

    // State lifecycle overrides — StateMachine IS a State
    void onStateMounted(Device *PDN) override {
        if (!launched) {
            initialize(PDN);
            launched = true;
        }
    }

    void onStateLoop(Device *PDN) override {
        currentState->onStateLoop(PDN);
        checkStateTransitions();
        if (stateChangeReady) {
            commitState(PDN);
        }
    }

    std::unique_ptr<Snapshot> onStatePaused(Device *PDN) override {
        paused = true;
        if (currentState) {
            currentSnapshot = currentState->onStatePaused(PDN);
        }
        return nullptr;
    }

    void onStateResumed(Device *PDN, Snapshot *snapshot) override {
        paused = false;
        if (currentState) {
            currentState->onStateResumed(PDN, currentSnapshot.get());
        }
    }

    void onStateDismounted(Device *PDN) override {
        if (currentState) {
            currentState->onStateDismounted(PDN);
        }
    }

    State *getCurrentState() {
        return currentState;
    }

    bool isPaused() const { return paused; }
    bool hasLaunched() const { return launched; }

protected:
    // initial state is 0 in the list here
    std::vector<State *> stateMap;

    bool stateChangeReady = false;

    State *newState = nullptr;
    State *currentState = nullptr;

    std::unique_ptr<Snapshot> currentSnapshot;
    bool launched = false;
    bool paused = false;
};
