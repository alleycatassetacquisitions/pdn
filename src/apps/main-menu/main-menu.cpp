#include "apps/main-menu/main-menu.hpp"

MainMenu::MainMenu(Device* PDN,RemotePlayerManager* remotePlayerManager) : StateMachine(MAIN_MENU_APP_ID) {
    this->remotePlayerManager = remotePlayerManager;
    this->remoteDeviceCoordinator = PDN->getRemoteDeviceCoordinator();
}

MainMenu::~MainMenu() {
    remotePlayerManager = nullptr;
}

void MainMenu::populateStateMap() {
    MenuIdleState* menuIdleState = new MenuIdleState(remotePlayerManager, remoteDeviceCoordinator);

    stateMap.push_back(menuIdleState);
}