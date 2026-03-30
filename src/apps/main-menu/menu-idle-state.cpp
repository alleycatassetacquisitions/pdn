#include "apps/main-menu/main-menu-states.hpp"
#include "apps/main-menu/main-menu-resources.hpp"


#define TAG "MENU_IDLE_STATE"

MenuIdleState::MenuIdleState(RemotePlayerManager* remotePlayerManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : State(MainMenuStateId::MAIN_MENU) {
    this->remotePlayerManager = remotePlayerManager;
    this->remoteDeviceCoordinator = remoteDeviceCoordinator;
}

MenuIdleState::~MenuIdleState() {
    remotePlayerManager = nullptr;
    remoteDeviceCoordinator = nullptr;
}

void MenuIdleState::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted");

    PDN->getDisplay()->
    invalidateScreen()->
        drawImage(glassesImage)->
        render();
}

void MenuIdleState::onStateLoop(Device *PDN) {
    LOG_I(TAG, "State loop");
}

void MenuIdleState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
}