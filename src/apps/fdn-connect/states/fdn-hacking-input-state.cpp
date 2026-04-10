#include "apps/fdn-connect/fdn-connect-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "FdnHackingInputState";

FdnHackingInputState::FdnHackingInputState(FDNConnectWirelessManager* wm, RemoteDeviceCoordinator* rdc, const uint8_t* seq)
    : State(FDN_HACKING_INPUT), wirelessManager(wm), remoteDeviceCoordinator(rdc), sequence(seq) {}

void FdnHackingInputState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Hacking input started — waiting for 8 presses");
    deviceRef = PDN;
    pressCount = 0;
    done = false;

    PDN->getPrimaryButton()->setButtonPress(onPrimaryPress, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(onSecondaryPress, this, ButtonInteraction::CLICK);

    timeoutTimer.setTimer(HACK_TIMEOUT_MS);
    showCurrentPrompt();
}

void FdnHackingInputState::onStateLoop(Device* PDN) {
    timeoutTimer.updateTime();
    if (!done && timeoutTimer.expired()) {
        LOG_W(TAG, "Hacking input timed out");
        done = true;
    }
}

void FdnHackingInputState::onStateDismounted(Device* PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    timeoutTimer.invalidate();
    deviceRef = nullptr;
    pressCount = 0;
    done = false;
}

bool FdnHackingInputState::transitionToIdle() {
    return done;
}

void FdnHackingInputState::onPrimaryPress(void* ctx) {
    static_cast<FdnHackingInputState*>(ctx)->handlePress(ButtonIdentifier::PRIMARY_BUTTON);
}

void FdnHackingInputState::onSecondaryPress(void* ctx) {
    static_cast<FdnHackingInputState*>(ctx)->handlePress(ButtonIdentifier::SECONDARY_BUTTON);
}

void FdnHackingInputState::handlePress(ButtonIdentifier button) {
    if (done || pressCount >= FDN_HACK_SEQUENCE_LENGTH) return;

    const uint8_t* mac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
    if (mac) {
        wirelessManager->sendButtonPress(mac, button);
    }

    LOG_I(TAG, "Press %d/%d: button %d", pressCount + 1, FDN_HACK_SEQUENCE_LENGTH, static_cast<int>(button));
    pressCount++;

    if (pressCount >= FDN_HACK_SEQUENCE_LENGTH) {
        LOG_I(TAG, "All 8 presses sent");
        done = true;
        return;
    }

    showCurrentPrompt();
}

void FdnHackingInputState::showCurrentPrompt() {
    if (deviceRef == nullptr || pressCount >= FDN_HACK_SEQUENCE_LENGTH) return;

    bool isPrimary = (sequence[pressCount] == static_cast<uint8_t>(ButtonIdentifier::PRIMARY_BUTTON));
    deviceRef->getDisplay()->invalidateScreen()
        ->drawImage(isPrimary ? largeButtonImage : smallButtonImage)
        ->render();
}
