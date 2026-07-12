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
    // The demo module PDN only uses a single cable (OUTPUT_JACK to FDN).
    // Requiring the aux jack would cause the disconnect policy to fire spuriously
    // and eject the PDN from Controller mode mid-game.
    bool isAuxRequired() override { return false; }
};

class Controller : public TypedStateMachine<PDN> {
public:
    Controller(Player* player,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               SymbolWirelessManager* symbolWirelessManager,
               ControllerWirelessManager* controllerWirelessManager);
    ~Controller();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;

private:
    static constexpr int kSymbolStateIndex     = 0;
    static constexpr int kController1StateIndex = 2;
    Player* player;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolWirelessManager* symbolWirelessManager;
    ControllerWirelessManager* controllerWirelessManager;
    ControllerDisconnectPolicy disconnectPolicy;
};