#pragma once

#include <functional>
#include <utility>
#include <vector>

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
    StateTransition(std::function<bool()> condition, State *nextState)
        : condition(std::move(condition)), nextState(nextState) {};

    // Method to check if the transition condition is met
    bool isConditionMet() const {
        return condition();
    }

    // Getter for the next state
    State *getNextState() const {
        return nextState;
    };

    std::function<bool()> condition; // Function pointer that returns true based on the global state
    State *nextState; // Pointer to the next state
};

class State {
public:
    virtual ~State() = default;

    explicit State(int stateId): name(stateId) {
    }

    virtual void addTransition(StateTransition *transition) {
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

    int getName() const { return name; };

    virtual void onStateMounted(Device *PDN) {};

    virtual void onStateLoop(Device *PDN) {};

    virtual void onStateDismounted(Device *PDN) {};

private:
    int name;
    std::vector<StateTransition *> transitions;
};