#include "apps/main-menu/main-menu-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "MAIN_MENU_STATE"

MainMenuState::MainMenuState(RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, MainMenuStateId::MAIN_MENU) {}

MainMenuState::~MainMenuState() {}

void MainMenuState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted");
    screenIndex = 0;
    fdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    displayTimer.setTimer(DISPLAY_TIMER_MS);
    renderScreen(fdn);
}

void MainMenuState::onStateLoop(FDN* fdn) {
    if (displayTimer.expired()) {
        screenIndex = (screenIndex + 1) % NUM_SCREENS;
        renderScreen(fdn);
        displayTimer.setTimer(DISPLAY_TIMER_MS);
    }
}

void MainMenuState::renderScreen(FDN* fdn) {
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawText(PHRASES[screenIndex][0], 0, 20)
        ->drawText(PHRASES[screenIndex][1], 0, 36)
        ->drawText(PHRASES[screenIndex][2], 0, 52)
        ->render();
}

void MainMenuState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    fdn->getDisplay()->invalidateScreen();
    displayTimer.invalidate();
    screenIndex = 0;
}

bool MainMenuState::transitionToSymbolMatch() {
    return !isConnected();
}
