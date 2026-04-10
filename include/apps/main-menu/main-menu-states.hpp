#pragma once

#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "game/player.hpp"
#include "device/device.hpp"



class MainMenuState : public ConnectState {
public:
    explicit MainMenuState(RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~MainMenuState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;
    bool transitionToIdle();

private:
    const char* MAIN_MENU_MESSAGE[2] = {"MAIN", "MENU"};
};