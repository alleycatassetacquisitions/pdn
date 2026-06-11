#pragma once

#include "state/state-machine.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"

class Hacking : public StateMachine {
public:
    Hacking(FDNConnectWirelessManager* fdnConnectWirelessManager,
            HackedPlayersManager* hackedPlayersManager,
            RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~Hacking();

    void populateStateMap() override;

private:
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    HackedPlayersManager* hackedPlayersManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};
