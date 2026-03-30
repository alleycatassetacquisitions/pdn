#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/player.hpp"
#include "device/device.hpp"

enum MainMenuStateId {
    MAIN_MENU = 0,
};

class MenuIdleState : public State {
public:
    explicit MenuIdleState(RemotePlayerManager* remotePlayerManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~MenuIdleState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToMainMenu();

private:
    RemotePlayerManager* remotePlayerManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};