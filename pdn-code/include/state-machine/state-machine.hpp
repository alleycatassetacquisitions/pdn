#pragma once

#include <vector>
#include "State.hpp"
#include "../device/device.hpp"

template <class State>
class StateMachine {
    
    public:
        StateMachine() {
            stateMap = populateStateMap();
            currentState = &stateMap[0];
        }
        
        virtual std::vector<State> populateStateMap();

        void checkStateTransitions()
        {
            newState = currentState.checkTransitions();
            stateChangeReady = (newState != nullptr);
        };
        
        void commitState() {
            currentState = newState;
            stateChangeReady = false;
            newState.onStateDismounted(&PDN);
            currentState.onStateMounted(&PDN);
        };

        void loop() {
            currentState.onStateLoop(&PDN);
            checkStateTransitions();
            if(stateChangeReady) {
                commitState();
            }
        }

    private:
        Device* PDN = Device::GetInstance();

        bool stateChangeReady = false;

        State newState;
        State currentState;
        // initial state is 0 in the list here
        std::vector<State> stateMap;

};