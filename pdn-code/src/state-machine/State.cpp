#include "../include/state-machine/State.hpp"

    State::State(StateId stateId) {
        name = stateId;
    };

    void State::addTransition(StateTransition* transition) {
        transitions.push_back(transition);
    };

    State* State::checkTransitions() {
        for (StateTransition* transition : transitions) {
            if (transition->isConditionMet()) {
                return transition->nextState;
            }
        }
        return nullptr;
    };

    StateId State::getName() {
        return name;
    };