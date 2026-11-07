#pragma once

#include "state/state-machine.hpp"
#include "device/remote-device-coordinator.hpp"

class MainMenuApp : public StateMachine {
public:
    /** Constructs the FDN main-menu app state machine. */
    explicit MainMenuApp(RemoteDeviceCoordinator* remoteDeviceCoordinator);

    void populateStateMap() override;

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};
