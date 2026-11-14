#include "device/firmware-update-state.hpp"

#include "device/animation/transmit-blink-animation.hpp"
#include "device/device.hpp"
#include "device/firmware-update-manager.hpp"

namespace {

constexpr LEDColor SEEDING_COLOR = LEDColor(255, 255, 255);
constexpr uint8_t SEEDING_BRIGHTNESS = 255;
constexpr unsigned long SEEDING_BLINK_MS = 800;
constexpr uint8_t SEEDING_FRAME_MS = 16;

}  // namespace

FirmwareUpdate::FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager)
    : TypedState<Device>(stateId)
    , firmwareUpdateManager(firmwareUpdateManager) {}

void FirmwareUpdate::onStateMounted(Device* device) {
    // A seed run sends on ESP-NOW directly, so it takes the radio from any HTTP trip.
    device->getWirelessManager()->enablePeerCommsMode();
    firmwareUpdateManager->beginSeeding();

    Display* d = device->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("UPDATING", 30);
    d->render();

    AnimationConfig config;
    config.speed = SEEDING_FRAME_MS;
    config.loop = true;
    device->getLightManager()->startAnimation(
        new TransmitBlinkAnimation(SEEDING_COLOR, SEEDING_BRIGHTNESS, SEEDING_BLINK_MS, SEEDING_BLINK_MS),
        config);
}

void FirmwareUpdate::onStateLoop(Device* device) {
    if (!firmwareUpdateManager->isSeeding()) {
        transitionToHostState = true;
    }
}

void FirmwareUpdate::onStateDismounted(Device* device) {
    device->getLightManager()->stopAnimation();
    transitionToHostState = false;
}

bool FirmwareUpdate::transitionToHost() const {
    return transitionToHostState;
}
