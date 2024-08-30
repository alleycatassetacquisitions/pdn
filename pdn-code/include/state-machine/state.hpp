#pragma once

#include <iostream>
#include <functional>
#include <vector>

#include "state-machine.hpp"
#include "../device/device.hpp"

class State;

struct StateId {
    explicit StateId(int stateid) {
        id = stateid;
    }

    int id;
};

class StateTransition {
public:
    // Constructor
    StateTransition(std::function<bool()> condition, State *nextState);

    // Method to check if the transition condition is met
    bool isConditionMet();

    // Getter for the next state
    State *getNextState();

    std::function<bool()> condition; // Function pointer that returns true based on the global state
    State *nextState; // Pointer to the next state
};

class State {
public:
    virtual ~State() = default;

    explicit State(StateId stateId): name(stateId) {
    }

    void addTransition(StateTransition *transition) {
        transitions.push_back(transition);
    };

    State *checkTransitions() {
        for (StateTransition* transition : transitions) {
            if (transition->isConditionMet()) {
                return transition->nextState;
            }
        }
        return nullptr;
    };

    StateId getName() { return name; };

    virtual void onStateMounted(Device *PDN) = 0;

    virtual void onStateLoop(Device *PDN) = 0;

    virtual void onStateDismounted(Device *PDN) = 0;

private:
    StateId name;
    std::vector<StateTransition *> transitions;
};

// Leaving this here for now, but I don't think it's actually needed.
// template<class StateMachine>
// class StateWithStateMachine : public State {
//
// public:
//     explicit StateWithStateMachine(StateMachine stateMachineForState, StateId stateId) : State(stateId) {
//         stateMachine = stateMachineForState;
//     };
//
//     virtual void onStateMounted(Device *PDN) {};
//     virtual void onStateLoop(Device *PDN) {
//         stateMachine.loop();
//     };
//     virtual void onStateDismounted(Device *PDN);
//
// private:
//     StateMachine stateMachine;
//
// };