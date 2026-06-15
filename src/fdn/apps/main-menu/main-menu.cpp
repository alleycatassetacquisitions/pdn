#include "apps/main-menu/main-menu.hpp"
#include "apps/main-menu/main-menu-states.hpp"
#include "apps/fdn-app-ids.hpp"

MainMenu::MainMenu(FDN* fdn, RemotePlayerManager* remotePlayerManager)
    : StateMachine(MAIN_MENU_APP_ID)
    , remotePlayerManager(remotePlayerManager)
    , remoteDeviceCoordinator(fdn->getRemoteDeviceCoordinator()) {}

MainMenu::~MainMenu() {
    remotePlayerManager     = nullptr;
    remoteDeviceCoordinator = nullptr;
}

void MainMenu::populateStateMap() {
    auto* mainMenuState = new MainMenuState(remoteDeviceCoordinator);

    mainMenuState->addAppTransition(
        std::bind(&MainMenuState::transitionToSymbolMatch, mainMenuState),
        StateId(SYMBOL_MATCH_APP_ID));

    stateMap.push_back(mainMenuState);
}
