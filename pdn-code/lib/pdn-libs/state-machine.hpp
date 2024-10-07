#pragma once

#include <vector>
#include "device.hpp"
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

class StateMachine {
public:
    StateMachine(Device *PDN) {
        this->PDN = PDN;
    }

    virtual ~StateMachine() {
        for (auto state: stateMap) {
            delete state;
        }
    };

    void initialize() {
        populateStateMap();
        currentState = stateMap[0];
        currentState->onStateMounted(PDN);
    }

    virtual void populateStateMap() = 0;

    void checkStateTransitions() {
        newState = currentState->checkTransitions();
        stateChangeReady = (newState != nullptr);
    };

    void commitState() {
        currentState->onStateDismounted(PDN);

        currentState = newState;
        stateChangeReady = false;

        currentState->onStateMounted(PDN);
    };

    void loop() {
        currentState->onStateLoop(PDN);
        checkStateTransitions();
        if (stateChangeReady) {
            commitState();
        }
    }

    State *getCurrentState() {
        return currentState;
    }

protected:
    // initial state is 0 in the list here
    std::vector<State *> stateMap;

private:
    Device *PDN;

    bool stateChangeReady = false;

    State *newState;
    State *currentState = nullptr;
};
