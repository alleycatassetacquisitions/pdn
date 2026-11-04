#pragma once

#include "state/state-machine.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"

class HackingApp : public StateMachine {
public:
    /** Constructs the FDN hacking app state machine. */
    HackingApp(FDNConnectWirelessManager* fdnConnectWirelessManager,
               HackedPlayersManager* hackedPlayersManager,
               RemoteDeviceCoordinator* remoteDeviceCoordinator);
    /** Destroys the FDN hacking app state machine. */
    ~HackingApp();

    void populateStateMap() override;

private:
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    HackedPlayersManager* hackedPlayersManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};
