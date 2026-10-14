#include "device/firmware-update-state.hpp"

#include "device/device.hpp"
#include "device/firmware-update-manager.hpp"

bool FirmwareUpdateHold::poll(Device* device) {
    // longPressedMillis() is 0 whenever the button is not in a long press.
    const unsigned long heldMs = device->getSecondaryButton()->longPressedMillis();
    if (heldMs == 0) {
        releasedSinceMount = true;
    }
    return releasedSinceMount && heldMs >= FIRMWARE_UPDATE_HOLD_MS;
}

void FirmwareUpdateHold::reset() {
    releasedSinceMount = false;
}

FirmwareUpdate::FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager)
    : State(stateId)
    , firmwareUpdateManager(firmwareUpdateManager) {}

void FirmwareUpdate::onStateMounted(Device* device) {
    device->getLightManager()->stopAnimation();
    firmwareUpdateManager->beginSeeding();

    Display* d = device->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("UPDATING", 30);
    d->render();
}

void FirmwareUpdate::onStateLoop(Device* device) {
    // A refused beginSeeding() leaves isSeeding() false, so this returns at once.
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
