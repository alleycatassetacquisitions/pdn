#pragma once

#include <iostream>
#include <functional>
#include <vector>

#include "../device/device.hpp"

class State;

struct StateId {
    StateId(int stateid) {
        id = stateid;
    }
    int id;
};

class StateTransition 
{
    public:
        // Constructor  
        StateTransition(std::function<bool()> condition, State* nextState);

        // Method to check if the transition condition is met
        bool isConditionMet();

        // Getter for the next state
        State* getNextState();

        std::function<bool()> condition; // Function pointer that returns true based on the global state
        State* nextState;                // Pointer to the next state
};

class State
{
public:
    State(StateId stateId);
    void addTransition(StateTransition* transition);
    State* checkTransitions();
    StateId getName();

    virtual void onStateMounted(Device PDN);
    virtual void onStateLoop(Device PDN);
    virtual void onStateDismounted(Device PDN);

private:
    StateId name;
    std::vector<StateTransition*> transitions;
};