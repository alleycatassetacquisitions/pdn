#pragma once

#include "state/typed-state-machine.hpp"
#include "state/connect-state.hpp"
#include "device/pdn.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/player.hpp"
#include "apps/pdn-app-ids.hpp"
#include "wireless/controller-wireless-manager.hpp"

class ControllerDisconnectPolicy : public ConnectState<PDN> {
public:
    explicit ControllerDisconnectPolicy(RemoteDeviceCoordinator* remoteDeviceCoordinator)
        : ConnectState<PDN>(remoteDeviceCoordinator, -1) {}

    bool isPrimaryRequired() override { return true; }
    bool isAuxRequired() override { return true; }
};

class Controller : public TypedStateMachine<PDN> {
public:
    Controller(Player* player,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               SymbolWirelessManager* symbolWirelessManager,
               ControllerWirelessManager* controllerWirelessManager);
    ~Controller();

    void populateStateMap() override;

private:
    Player* player;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolWirelessManager* symbolWirelessManager;
    ControllerWirelessManager* controllerWirelessManager;
    ControllerDisconnectPolicy disconnectPolicy;
};