#pragma once

#include "state/state-machine.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "apps/hacking/hacked-players-manager.hpp"

class IdleApp : public StateMachine {
public:
    /** Constructs the FDN idle app state machine. */
    IdleApp(RemotePlayerManager* remotePlayerManager, HackedPlayersManager* hackedPlayersManager,
            FDNConnectWirelessManager* fdnConnectWirelessManager,
            RemoteDeviceCoordinator* remoteDeviceCoordinator);
    /** Clears non-owning manager pointers. */
    ~IdleApp();

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};
