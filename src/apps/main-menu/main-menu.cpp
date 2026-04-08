#include "apps/main-menu/main-menu.hpp"
#include "apps/main-menu/main-menu-states.hpp"
#include "device/drivers/logger.hpp"

#define TAG "MAIN_MENU"

MainMenu::MainMenu(Device* PDN, RemotePlayerManager* remotePlayerManager) : StateMachine(MAIN_MENU_APP_ID) {
    this->remotePlayerManager = remotePlayerManager;
    this->remoteDeviceCoordinator = PDN->getRemoteDeviceCoordinator();
}

MainMenu::~MainMenu() {
    remotePlayerManager = nullptr;
}

void MainMenu::populateStateMap() {
    auto* mainMenuState = new MainMenuState(remoteDeviceCoordinator);
    stateMap.push_back(mainMenuState); // [0] MAIN_MENU
}