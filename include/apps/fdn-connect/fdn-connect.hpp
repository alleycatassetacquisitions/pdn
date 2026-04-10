#pragma once

#include "state/state-machine.hpp"
#include "apps/fdn-connect/fdn-connect-wireless-manager.hpp"
#include "apps/fdn-connect/fdn-connect-states.hpp"
#include "game/player.hpp"
#include "device/device.hpp"

constexpr int FDN_CONNECT_APP_ID = 3;

class FDNConnect : public StateMachine {
public:
    FDNConnect(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, FDNConnectWirelessManager* fdnConnectWirelessManager);
    ~FDNConnect();
    void populateStateMap() override;

    // Exit condition checked by the parent Quickdraw state machine
    bool returnToIdle();

private:
    Player* player;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    FDNConnectWirelessManager* fdnConnectWirelessManager;
};
