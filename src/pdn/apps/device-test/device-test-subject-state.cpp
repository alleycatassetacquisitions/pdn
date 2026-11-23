#include "apps/device-test/device-test-subject-state.hpp"

#include "device/drivers/haptics.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/serial-wrapper.hpp"

#ifndef FIRMWARE_COMMIT_HASH
#define FIRMWARE_COMMIT_HASH "unknown"
#endif

static const char TAG[] = "DeviceTestSubject";

DeviceTestSubjectState::DeviceTestSubjectState(
RemoteDeviceCoordinator* remoteDeviceCoordinatorIn,
DeviceTestWirelessManager* deviceTestWirelessManagerIn)
    : TypedState<PDN>(PlayerFetchStateId::DEVICE_TEST_SUBJECT)
    , remoteDeviceCoordinator(remoteDeviceCoordinatorIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn) {}

DeviceTestSubjectState::~DeviceTestSubjectState() {
    remoteDeviceCoordinator = nullptr;
    deviceTestWirelessManager = nullptr;
}

void DeviceTestSubjectState::onStateMounted(PDN* pdn) {
    transitionToPinEntryState = false;
    hapticPulseActive = false;
    mountedPdn = pdn;

    pdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawCenteredText("TEST MODE", 32)
    ->render();

    deviceTestWirelessManager->registerEspNowHandler();
    deviceTestWirelessManager->setPeerFilter(
    [this](const uint8_t* mac) { return remoteDeviceCoordinator->isDirectPeer(mac); });
    deviceTestWirelessManager->setPacketReceivedCallback(
    [this](const uint8_t* fromMac, DeviceTestCommand command, const uint8_t* payload,
           size_t payloadLen) { onDeviceTestPacket(fromMac, command, payload, payloadLen); });

    pdn->getPrimaryButton()->setButtonPress(onPrimaryButtonPress, this, ButtonInteraction::PRESS);
    pdn->getSecondaryButton()->setButtonPress(onSecondaryButtonPress, this,
                                              ButtonInteraction::PRESS);
}

void DeviceTestSubjectState::onStateLoop(PDN* pdn) {
    mountedPdn = pdn;
    if (hapticPulseActive && hapticPulseTimer.expired()) {
        pdn->getHaptics()->off();
        hapticPulseActive = false;
    }

    const uint8_t* inputPeer = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::INPUT_JACK);
    const uint8_t* outputPeer = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
    if (inputPeer == nullptr && outputPeer == nullptr) {
        transitionToPinEntryState = true;
    }
}

void DeviceTestSubjectState::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getHaptics()->off();
    clearLeds(pdn);
    deviceTestWirelessManager->clearCallbacks();
    deviceTestWirelessManager->unregisterEspNowHandler();
    mountedPdn = nullptr;
}

bool DeviceTestSubjectState::transitionToPinEntry() const { return transitionToPinEntryState; }

void DeviceTestSubjectState::onDeviceTestPacket(const uint8_t* fromMac, DeviceTestCommand command,
                                                const uint8_t* payload, size_t payloadLen) {
    (void)payload;
    (void)payloadLen;
    PDN* pdn = mountedPdn;
    if (pdn == nullptr) {
        return;
    }

    switch (command) {
        case DeviceTestCommand::BEGIN_SESSION:
            deviceTestWirelessManager->sendSessionInfo(fromMac, FIRMWARE_COMMIT_HASH);
            break;
        case DeviceTestCommand::SHOW_SCREEN_PATTERN: showScreenPattern(pdn); break;
        case DeviceTestCommand::SHOW_ALL_LEDS: showAllLeds(pdn); break;
        case DeviceTestCommand::CLEAR_LEDS: clearLeds(pdn); break;
        case DeviceTestCommand::TRIGGER_HAPTICS: pulseHaptics(pdn); break;
        case DeviceTestCommand::END_SESSION:
            clearLeds(pdn);
            transitionToPinEntryState = true;
            break;
        default: break;
    }
}

void DeviceTestSubjectState::pulseHaptics(PDN* pdn) {
    pdn->getHaptics()->setIntensity(VIBRATION_MAX);
    hapticPulseTimer.setTimer(HAPTIC_PULSE_DURATION_MS);
    hapticPulseActive = true;
}

void DeviceTestSubjectState::showAllLeds(PDN* pdn) { pdn->getLightManager()->showSolidWhiteLeds(); }

void DeviceTestSubjectState::clearLeds(PDN* pdn) { pdn->getLightManager()->clear(); }

void DeviceTestSubjectState::showScreenPattern(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen()->whiteScreen()->render();
}

void DeviceTestSubjectState::onPrimaryButtonPress(void* ctx) {
    DeviceTestSubjectState* state = static_cast<DeviceTestSubjectState*>(ctx);
    if (state->mountedPdn == nullptr) {
        return;
    }
    const uint8_t* peer = state->remoteDeviceCoordinator->getPeerMac(SerialIdentifier::INPUT_JACK);
    if (peer == nullptr) {
        peer = state->remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
    }
    if (peer == nullptr) {
        return;
    }
    uint8_t payload[1] = {static_cast<uint8_t>(ButtonIdentifier::PRIMARY_BUTTON)};
    state->deviceTestWirelessManager->sendCommand(peer, DeviceTestCommand::BUTTON_EVENT, payload,
                                                  sizeof(payload));
}

void DeviceTestSubjectState::onSecondaryButtonPress(void* ctx) {
    DeviceTestSubjectState* state = static_cast<DeviceTestSubjectState*>(ctx);
    if (state->mountedPdn == nullptr) {
        return;
    }
    const uint8_t* peer = state->remoteDeviceCoordinator->getPeerMac(SerialIdentifier::INPUT_JACK);
    if (peer == nullptr) {
        peer = state->remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
    }
    if (peer == nullptr) {
        return;
    }
    uint8_t payload[1] = {static_cast<uint8_t>(ButtonIdentifier::SECONDARY_BUTTON)};
    state->deviceTestWirelessManager->sendCommand(peer, DeviceTestCommand::BUTTON_EVENT, payload,
                                                  sizeof(payload));
}
