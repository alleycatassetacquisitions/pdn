#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "state/state-machine.hpp"
#include "game/node-network-states.hpp"
#include "game/node-match-manager.hpp"
#include "apps/player-registration/player-registration.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"

constexpr int NODE_NETWORK_APP_ID = 2;

class NodeNetwork : public StateMachine {
public:
    NodeNetwork(Player* player, Device* pdn, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager);
    ~NodeNetwork() override;

    void populateStateMap() override;

private:
    NodeMatchManager* matchManager = nullptr;
    Player* player = nullptr;
    WirelessManager* wirelessManager = nullptr;
    StorageInterface* storageManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
    RemoteDebugManager* remoteDebugManager = nullptr;
};
