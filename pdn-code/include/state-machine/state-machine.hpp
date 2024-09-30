#pragma once

#include <vector>
#include "../device/device.hpp"
#include "state.hpp"

/* state -> onStateMounted
 * statemachine -> onStateMounting
 *
*/

class StateMachine {
    
    public:

        StateMachine() = default;

        virtual ~StateMachine() {
            for (auto state : stateMap) {
                delete state;
            }
        };

        void initialize() {
            populateStateMap();
            currentState = stateMap[0];
        }

        virtual void populateStateMap() = 0;

        void checkStateTransitions()
        {
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
            if(stateChangeReady) {
                commitState();
            }
        }

        State* getCurrentState() {
            return currentState;
        }

    protected:
        // initial state is 0 in the list here
        std::vector<State*> stateMap;

    private:
        Device* PDN = Device::GetInstance();

        bool stateChangeReady = false;

        State* newState;
        State* currentState;


};
