#pragma once

#include "state/state-machine.hpp"
#include "apps/fdn-connect/fdn-connect-wireless-manager.hpp"
#include "apps/fdn-connect/fdn-connect-states.hpp"
#include "game/player.hpp"
#include "device/device.hpp"

class FDNConnect : public StateMachine {
public:
    FDNConnect(Player *player, RemoteDeviceCoordinator* remoteDeviceCoordinator, FDNConnectWirelessManager* fdnConnectWirelessManager);

    void populateStateMap() override;

private:
    Player *player;
    Device *PDN;
    FDNConnectWirelessManager* fdnConnectWirelessManager;
};