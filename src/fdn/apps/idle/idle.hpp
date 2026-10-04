#pragma once

#include "state/state-machine.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "apps/hacking/hacked-players-manager.hpp"

class FirmwareUpdateManager;

class Idle : public StateMachine {
public:
    Idle(RemotePlayerManager* remotePlayerManager,
         HackedPlayersManager* hackedPlayersManager,
         FDNConnectWirelessManager* fdnConnectWirelessManager,
         RemoteDeviceCoordinator* remoteDeviceCoordinator,
         FirmwareUpdateManager* firmwareUpdateManager);
    ~Idle();

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    FirmwareUpdateManager* firmwareUpdateManager;
};
