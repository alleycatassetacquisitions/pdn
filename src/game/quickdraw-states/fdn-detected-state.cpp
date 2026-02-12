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
        handshakeComplete = true;

        // Track which game type we're launching
        player->setLastFdnGameType(static_cast<int>(pendingGameType));

        // Look up the app ID for this game type
        int appId = getAppIdForGame(pendingGameType);
        if (appId < 0) {
            LOG_W(TAG, "No app registered for game type %d", static_cast<int>(pendingGameType));
            transitionToIdleState = true;
            return;
        }

        auto* game = dynamic_cast<MiniGame*>(PDN->getApp(StateId(appId)));
        if (!game) {
            LOG_W(TAG, "App %d not found in AppConfig", appId);
            transitionToIdleState = true;
            return;
        }

        // Configure the minigame based on game type
        if (pendingGameType == GameType::SIGNAL_ECHO) {
            auto* echo = dynamic_cast<SignalEcho*>(game);
            if (echo) {
                // Difficulty gating: EASY if no boon, HARD if boon
                SignalEchoConfig config = player->hasKonamiBoon() ? SIGNAL_ECHO_HARD : SIGNAL_ECHO_EASY;
                config.managedMode = true;
                echo->getConfig() = config;
            }
        }

        LOG_I(TAG, "Launching %s (%s mode)",
               getGameDisplayName(pendingGameType),
               player->hasKonamiBoon() ? "HARD" : "EASY");

        game->resetGame();
        PDN->setActiveApp(StateId(appId));
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
