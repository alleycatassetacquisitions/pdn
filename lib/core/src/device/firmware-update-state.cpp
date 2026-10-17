#include "device/firmware-update-state.hpp"

#include "device/device.hpp"
#include "device/firmware-update-manager.hpp"

FirmwareUpdate::FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager)
    : TypedState<Device>(stateId)
    , firmwareUpdateManager(firmwareUpdateManager) {}

void FirmwareUpdate::onStateMounted(Device* device) {
    device->getLightManager()->stopAnimation();
    // A seed run sends on ESP-NOW directly, so it takes the radio from any HTTP trip.
    device->getWirelessManager()->enablePeerCommsMode();
    firmwareUpdateManager->beginSeeding();

    Display* d = device->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("UPDATING", 30);
    d->render();
}

void FirmwareUpdate::onStateLoop(Device* device) {
    // An unsigned image makes beginSeeding() refuse, so the host gets its screen straight back.
    if (!firmwareUpdateManager->isSeeding()) {
        transitionToHostState = true;
    }
}

void FirmwareUpdate::onStateDismounted(Device* device) {
    transitionToHostState = false;
}

bool FirmwareUpdate::transitionToHost() const {
    return transitionToHostState;
}
