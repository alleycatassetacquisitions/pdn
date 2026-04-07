#pragma once

#include "state/state-machine.hpp"
#include "apps/main-menu/main-menu-states.hpp"
#include "device/device.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"

constexpr int MAIN_MENU_APP_ID = 1;

enum MainMenuStateId {
    MAIN_MENU = 0,
};

class MainMenu : public StateMachine {
public:
    MainMenu(Device* PDN, RemotePlayerManager* remotePlayerManager);
    ~MainMenu();

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};