#pragma once

#include <vector>
#include "device/device.hpp"
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
 *
 * Lifecycle dispatch goes through StateLifecycle* so that the bridge methods
 * (mount/loop/dismount) remain private to state implementers while still being
 * reachable via virtual dispatch from StateMachine.
*/

class StateMachine : public State {
public:
    explicit StateMachine(int stateId) : State(stateId) {}

    ~StateMachine() override {
        for (auto state: stateMap) {
            delete state;
        }
    };

    void initialize(Device *PDN) {
        // Populate once. onStateMounted lands here on every swap back to this
        // app, and a second populateStateMap appends a whole duplicate state set
        // that is never mounted (currentState addresses the original slots) and
        // is only freed by ~StateMachine.
        if (stateMap.empty()) {
            populateStateMap();
        }
        currentState = findEntryState();
        asLifecycle(currentState)->mount(PDN);
        launched = true;
    }

    /// The state the next mount enters at, or unset for the boot state. Both paths
    /// on Device write it before mounting — setActiveApp from the app transition
    /// that named one, loadAppConfig with unset — so neither inherits an earlier
    /// mount's value. A StateMachine driven outside an AppConfig never writes it at
    /// all (HelloLinkMachine calls initialize() directly), and rides the unset
    /// default, so that default is load-bearing and not redundant.
    void setEntryState(StateId stateId) {
        entryStateId = stateId;
    }

    /// The states in registration order. Index 0 is the boot state; app
    /// transitions name their target by state id, not by position here.
    const std::vector<State*>& getStateMap() const {
        return stateMap;
    }

    /**
     * Skip to a specific state by index, bypassing intermediate states.
     * Useful for testing scenarios where you want to start at a later state.
     * @param stateIndex The index in stateMap to skip to
     * @return true if successful, false if index out of range
     */
    bool skipToState(Device *PDN, int stateIndex) {
        if (stateIndex < 0 || stateIndex >= static_cast<int>(stateMap.size())) {
            return false;
        }
        if (currentState) {
            asLifecycle(currentState)->dismount(PDN);
        }
        currentState = stateMap[stateIndex];
        asLifecycle(currentState)->mount(PDN);
        return true;
    }

    virtual void populateStateMap() = 0;

    void checkStateTransitions() {
        pendingTransition = currentState->checkTransitions();
    };

    /// Moves to the sibling state the pending edge names. Only valid when one is
    /// held and it is an intra-machine edge; a hand-off leaves via setActiveApp.
    void commitState(Device* device) {
        State* nextState = pendingTransition->getNextState();
        asLifecycle(currentState)->dismount(device);

        currentState = nextState;
        pendingTransition = nullptr;

        asLifecycle(currentState)->mount(device);
    };

    State *getCurrentState() {
        return currentState;
    }

    void onStateMounted(Device *PDN) override {
        initialize(PDN);
    }

    void onStateLoop(Device *PDN) override {
        asLifecycle(currentState)->loop(PDN);
        checkStateTransitions();
        if (pendingTransition == nullptr) {
            return;
        }
        // A null next state is what marks the winning edge as a hand-off.
        if (pendingTransition->getNextState() != nullptr) {
            commitState(PDN);
            return;
        }

        // On the success path setActiveApp dismounts this machine, and
        // onStateDismounted clears the pending edge as it goes. An unregistered id
        // returns without dismounting, and the same edge wins again next tick.
        StateId nextApp = pendingTransition->getTargetAppId();
        StateId entryState = pendingTransition->getEntryStateId();
        PDN->setActiveApp(nextApp, entryState);
    }

    void onStateDismounted(Device *PDN) override {
        asLifecycle(currentState)->dismount(PDN);
        currentState = nullptr;
        pendingTransition = nullptr;
    }

    bool hasLaunched() const {
        return launched;
    }

protected:
    // initial state is 0 in the list here
    std::vector<State *> stateMap;

    State *currentState = nullptr;

    /// The winning edge from the last checkStateTransitions, or null when none
    /// held. Its own fields say where it goes, so nothing else needs recording.
    StateTransition* pendingTransition = nullptr;

private:
    // Upcast helper — mount/loop/dismount are private on State* so we dispatch
    // through StateLifecycle* where they are public, allowing virtual dispatch to
    // reach the correct override without exposing the bridge to state authors.
    static StateLifecycle* asLifecycle(State* state) {
        return static_cast<StateLifecycle*>(state);
    }

    // Resolved against this machine's own map, so ids only have to be distinct
    // within one app for the scan to be unambiguous.
    State* findEntryState() {
        if (entryStateId.id < 0) {
            return stateMap[0];
        }
        for (State* state : stateMap) {
            if (state->getStateId() == entryStateId.id) {
                return state;
            }
        }
        LOG_E("StateMachine", "app %d has no state %d; entering boot state",
              getStateId(), entryStateId.id);
        return stateMap[0];
    }

    bool launched = false;
    StateId entryStateId = StateId(-1);
};
