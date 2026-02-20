#pragma once

#include "state/state-machine.hpp"
#include "state/instructions-state.hpp"
#include "apps/speeder/speeder-app-states.hpp"

const int SPEEDER_APP_ID = 100;

class SpeederApp : public StateMachine {
public:
    SpeederApp();
    ~SpeederApp() override;

    void populateStateMap() override;

private:
    InstructionsPage instructionsPages[3] = {
        { {"Avoid the", "barriers!!!"}, 2 },
        { {"Dodge", "with the", "buttons"}, 3 },
        { {"Top: move up", "Bottom:", "move down"}, 3 }
    };
    InstructionsConfig instructionsConfig = { instructionsPages, 3, "Proceed", "Repeat"};

    InstructionsConfig tryAgainConfig = { nullptr, 0, "Try Again", "Exit" };
};