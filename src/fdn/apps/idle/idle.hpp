#pragma once

#include "state/state-machine.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "alleycat-server/fdn-match-spool.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"

class IdleApp : public StateMachine {
public:
    /** Constructs the FDN idle app state machine. */
    IdleApp(RemotePlayerManager* remotePlayerManager,
            RemoteDeviceCoordinator* remoteDeviceCoordinator, FdnMatchSpool* fdnMatchSpool,
            FdnProvisioningManager* fdnProvisioningManager);

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    FdnMatchSpool* fdnMatchSpool;
    FdnProvisioningManager* fdnProvisioningManager;
};
