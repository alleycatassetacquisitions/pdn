#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include "device/device-constants.hpp"
#include "device/device-types.hpp"
#include <cstdint>

static const char* TAG = "FdnDetected";

FdnDetected::FdnDetected(Player* player) :
    State(FDN_DETECTED),
    player(player)
{
}

FdnDetected::~FdnDetected() {
    player = nullptr;
}

void FdnDetected::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    transitionToFdnCompleteState = false;
    fackReceived = false;
    macSent = false;
    handshakeComplete = false;

    // Read the pending FDN message from Player (set by Idle)
    fdnMessage = player->getPendingCdevMessage();

    LOG_I(TAG, "FDN detected, message: %s", fdnMessage.c_str());

    // Parse the FDN message
    if (!parseFdnMessage(fdnMessage, pendingGameType, pendingReward)) {
        LOG_W(TAG, "Failed to parse FDN message: %s", fdnMessage.c_str());
        transitionToIdleState = true;
        return;
    }

    LOG_I(TAG, "Challenge: %s, reward: %s",
           getGameDisplayName(pendingGameType),
           getKonamiButtonName(pendingReward));

    // Set up serial callback to listen for fack
    PDN->setOnStringReceivedCallback([this](const std::string& message) {
        LOG_I(TAG, "Serial received: %s", message.c_str());
        if (message == FDN_ACK) {
            LOG_I(TAG, "Received fack from NPC");
            fackReceived = true;
        }
    });

    // Send our MAC address to the NPC
    uint8_t* macAddr = PDN->getWirelessManager()->getMacAddress();
    const char* macStr = MacToString(macAddr);
    std::string macMessage = SEND_MAC_ADDRESS + std::string(macStr);
    PDN->writeString(macMessage.c_str());
    macSent = true;

    LOG_I(TAG, "Sent MAC: %s", macStr);

    // Start timeout
    timeoutTimer.setTimer(TIMEOUT_MS);

    // Display challenge info
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("CHALLENGE", 20, 15);
    PDN->getDisplay()->drawText(getGameDisplayName(pendingGameType), 20, 35);
    PDN->getDisplay()->drawText("CONNECTING...", 10, 55);
    PDN->getDisplay()->render();
}

void FdnDetected::onStateLoop(Device* PDN) {
    if (fackReceived && !handshakeComplete) {
        LOG_I(TAG, "fack received, launching Signal Echo");
        handshakeComplete = true;

        // Configure Signal Echo with hard mode and managed mode
        auto* echo = dynamic_cast<SignalEcho*>(PDN->getApp(StateId(SIGNAL_ECHO_APP_ID)));
        if (echo) {
            SignalEchoConfig config = SIGNAL_ECHO_HARD;
            config.managedMode = true;
            echo->getConfig() = config;
            echo->resetGame();

            // Switch to Signal Echo — Quickdraw pauses at this state
            PDN->setActiveApp(StateId(SIGNAL_ECHO_APP_ID));
        } else {
            LOG_W(TAG, "Signal Echo not registered in AppConfig");
            transitionToIdleState = true;
        }
        return;
    }

    if (timeoutTimer.expired()) {
        LOG_W(TAG, "FDN handshake timed out");
        transitionToIdleState = true;
    }
}

void FdnDetected::onStateDismounted(Device* PDN) {
    timeoutTimer.invalidate();
    fdnMessage.clear();
    fackReceived = false;
    macSent = false;
    player->clearPendingChallenge();
    PDN->clearCallbacks();
}

std::unique_ptr<Snapshot> FdnDetected::onStatePaused(Device* PDN) {
    auto snapshot = std::make_unique<FdnDetectedSnapshot>();
    snapshot->gameType = pendingGameType;
    snapshot->reward = pendingReward;
    snapshot->handshakeComplete = handshakeComplete;
    return snapshot;
}

void FdnDetected::onStateResumed(Device* PDN, Snapshot* snapshot) {
    if (snapshot) {
        auto* fdnSnapshot = static_cast<FdnDetectedSnapshot*>(snapshot);
        pendingGameType = fdnSnapshot->gameType;
        pendingReward = fdnSnapshot->reward;
        handshakeComplete = fdnSnapshot->handshakeComplete;
    }

    // If handshake was complete when we paused, the minigame is done
    if (handshakeComplete) {
        transitionToFdnCompleteState = true;
    }
}

bool FdnDetected::transitionToIdle() {
    return transitionToIdleState;
}

bool FdnDetected::transitionToFdnComplete() {
    return transitionToFdnCompleteState;
}
