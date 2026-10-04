#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/firmware-update-manager.hpp"

FirmwareUpdate::FirmwareUpdate(FirmwareUpdateManager* firmwareUpdateManager)
    : TypedState<PDN>(FIRMWARE_UPDATE)
    , firmwareUpdateManager(firmwareUpdateManager) {}

void FirmwareUpdate::onStateMounted(PDN* pdn) {
    pdn->getLightManager()->stopAnimation();
    // A run already streaming (e.g. a repeat hold) is left alone rather than
    // restarted; beginSeeding() itself refuses that case.
    firmwareUpdateManager->beginSeeding();

    Display* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("UPDATING", 30);
    d->render();
}

void FirmwareUpdate::onStateLoop(PDN* pdn) {
    // beginSeeding() failing (unsigned device, unreadable trailer) leaves
    // isSeeding() false from the first tick, which reads the same as a run
    // that already ended: straight back to Idle.
    if (!firmwareUpdateManager->isSeeding()) {
        transitionToIdleState = true;
    }
}

void FirmwareUpdate::onStateDismounted(PDN* pdn) {
    transitionToIdleState = false;
}

bool FirmwareUpdate::transitionToIdle() {
    return transitionToIdleState;
}
