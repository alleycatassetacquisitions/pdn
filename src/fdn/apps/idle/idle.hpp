#pragma once

#include "state/state-machine.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "alleycat-server/fdn-match-spool.hpp"
#include "apps/hacking/hacked-players-manager.hpp"

class IdleApp : public StateMachine {
public:
    /** Constructs the FDN idle app state machine. */
    IdleApp(RemotePlayerManager* remotePlayerManager, HackedPlayersManager* hackedPlayersManager,
            FDNConnectWirelessManager* fdnConnectWirelessManager,
            RemoteDeviceCoordinator* remoteDeviceCoordinator, FdnMatchSpool* fdnMatchSpool);

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    FdnMatchSpool* fdnMatchSpool;
};
