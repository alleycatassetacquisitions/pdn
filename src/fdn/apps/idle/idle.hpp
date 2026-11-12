#pragma once

#include "state/state-machine.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"

class IdleApp : public StateMachine {
public:
    /** Constructs the FDN idle app state machine. */
    IdleApp(RemotePlayerManager* remotePlayerManager,
            RemoteDeviceCoordinator* remoteDeviceCoordinator);

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};
