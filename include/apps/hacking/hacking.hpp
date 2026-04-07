#pragma once

#include "device/device.hpp"
#include "state/state-machine.hpp"

constexpr int HACKING_APP_ID = 3;

enum HackingStateId {
    CONNECTION_DETECTED = 0,
    HACKING_HINT = 1,
    HACKING_INPUT = 2,
};

class Hacking : public StateMachine {
public:
    Hacking(Device* PDN);
    ~Hacking();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    void populateStateMap() override;

private:

};