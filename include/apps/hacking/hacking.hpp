#pragma once

#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "state/state-machine.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "apps/app-ids.hpp"

enum HackingStateId {
    HACKING_HINT  = 0,
    HACKING_INPUT = 1,
    HACK_SUCCESS  = 2,
    HACK_LOCKOUT  = 3,
};

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
