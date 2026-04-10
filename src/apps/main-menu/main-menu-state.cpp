#include "apps/main-menu/main-menu-states.hpp"
#include "apps/main-menu/main-menu.hpp"
#include "device/drivers/logger.hpp"

#define TAG "MAIN_MENU_STATE"

MainMenuState::MainMenuState(RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState(remoteDeviceCoordinator, MainMenuStateId::MAIN_MENU) {}

MainMenuState::~MainMenuState() {}

void MainMenuState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Mounted");
    PDN->getDisplay()
        ->invalidateScreen()
        ->drawText(MAIN_MENU_MESSAGE[0], 40, 24)
        ->drawText(MAIN_MENU_MESSAGE[1], 40, 40)
        ->render();
}

void MainMenuState::onStateLoop(Device* PDN) {}

void MainMenuState::onStateDismounted(Device* PDN) {
    LOG_I(TAG, "Dismounted");
}

bool MainMenuState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}

bool MainMenuState::transitionToIdle() {
    return !isConnected();
}