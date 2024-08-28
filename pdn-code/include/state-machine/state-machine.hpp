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
        virtual void onStateMounting(State* state) = 0;
        virtual void onStateDismounting(State* state) = 0;
        virtual void onStateLooping(State* state) = 0;

        void checkStateTransitions()
        {
            newState = currentState.checkTransitions();
            stateChangeReady = (newState != nullptr);
        };
        
        void commitState() {

            onStateDismounting(*currentState);
            currentState.onStateDismounted(PDN);
            
            currentState = newState;
            stateChangeReady = false;
            
            onStateMounting(*currentState);
            currentState.onStateMounted(PDN);
        };

        void loop() {
            onStateLooping(*currentState);
            currentState.onStateLoop(PDN);
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