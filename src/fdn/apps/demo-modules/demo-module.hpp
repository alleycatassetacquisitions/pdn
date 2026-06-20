#pragma once

#include "state/typed-state-machine.hpp"
#include "device/fdn.hpp"
#include "device/remote-device-coordinator.hpp"

class DemoModule : public TypedStateMachine<FDN> {
public:
    DemoModule(int stateId);
    ~DemoModule();

    void populateStateMap() override;
};