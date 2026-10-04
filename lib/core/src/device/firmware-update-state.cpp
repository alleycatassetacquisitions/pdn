#include "device/firmware-update-state.hpp"

#include "device/device.hpp"
#include "device/firmware-update-manager.hpp"

FirmwareUpdate::FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager)
    : State(stateId)
    , firmwareUpdateManager(firmwareUpdateManager) {}

void FirmwareUpdate::onStateMounted(Device* device) {
    device->getLightManager()->stopAnimation();
    // A run already streaming (e.g. a repeat hold) is left alone rather than
    // restarted; beginSeeding() itself refuses that case.
    firmwareUpdateManager->beginSeeding();

    Display* d = device->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("UPDATING", 30);
    d->render();
}

void FirmwareUpdate::onStateLoop(Device* device) {
    // beginSeeding() failing (unsigned device, unreadable trailer) leaves
    // isSeeding() false from the first tick, which reads the same as a run
    // that already ended: straight back to idle.
    if (!firmwareUpdateManager->isSeeding()) {
        transitionToIdleState = true;
    }
}

void FirmwareUpdate::onStateDismounted(Device* device) {
    transitionToIdleState = false;
}

bool FirmwareUpdate::transitionToIdle() {
    return transitionToIdleState;
}
