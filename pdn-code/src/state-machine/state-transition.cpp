#include "../include/state-machine/state.hpp"

StateTransition::StateTransition(std::function<bool()> condition, State* nextState)
        : condition(condition), nextState(nextState) {}

// Method to check if the transition condition is met
bool StateTransition::isConditionMet() {
    return condition();
}

// Getter for the next state
State* StateTransition::getNextState() {
    return nextState;
}