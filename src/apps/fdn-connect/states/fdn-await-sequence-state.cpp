#include "apps/fdn-connect/fdn-connect-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include <cstring>

static const char* TAG = "FdnAwaitSequenceState";

FdnAwaitSequenceState::FdnAwaitSequenceState(FDNConnectWirelessManager* wm)
    : State(FDN_AWAIT_SEQUENCE), wirelessManager(wm) {}

void FdnAwaitSequenceState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Waiting for hack sequence from FDN");

    wirelessManager->setHackSequenceCallback([this](const uint8_t seq[FDN_HACK_SEQUENCE_LENGTH]) {
        memcpy(sequence, seq, FDN_HACK_SEQUENCE_LENGTH);
        sequenceReceived = true;
        LOG_I(TAG, "Hack sequence received");
    });

    timeoutTimer.setTimer(AWAIT_TIMEOUT_MS);

    static const char* waitMsg[2] = {"FDN", "CONNECT"};
    PDN->getDisplay()->invalidateScreen()->drawText(waitMsg[0], 0, 20)->drawText(waitMsg[1], 0, 40)->render();
}

void FdnAwaitSequenceState::onStateLoop(Device* PDN) {
    timeoutTimer.updateTime();
    if (!timedOut && timeoutTimer.expired()) {
        LOG_I(TAG, "Await timeout — FDN likely recognised a returning player");
        timedOut = true;
    }
}

void FdnAwaitSequenceState::onStateDismounted(Device* PDN) {
    wirelessManager->clearCallbacks();
    timeoutTimer.invalidate();
    timedOut = false;
    sequenceReceived = false;
}

bool FdnAwaitSequenceState::transitionToHackingInput() {
    return sequenceReceived;
}

bool FdnAwaitSequenceState::transitionToIdle() {
    return timedOut;
}

const uint8_t* FdnAwaitSequenceState::getSequence() const {
    return sequence;
}
