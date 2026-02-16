#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/difficulty-scaler.hpp"
#include "game/difficulty-helpers.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include "device/device-constants.hpp"
#include "device/device-types.hpp"
#include <cstdint>

static const char* TAG = "FdnDetected";

FdnDetected::FdnDetected(Player* player, DifficultyScaler* scaler) :
    State(FDN_DETECTED),
    player(player),
    scaler(scaler)
{
}

FdnDetected::~FdnDetected() {
    player = nullptr;
    scaler = nullptr;
}

void FdnDetected::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    transitionToFdnCompleteState = false;
    transitionToReencounterState = false;
    transitionToKonamiPuzzleState = false;
    transitionToConnectionLostState = false;
    fackReceived = false;
    macSent = false;
    handshakeComplete = false;
    gameLaunched = false;

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

        // Store game info on Player for KonamiMetaGame Handshake to read
        player->setLastFdnGameType(static_cast<int>(pendingGameType));
        player->setLastFdnReward(static_cast<uint8_t>(pendingReward));

        LOG_I(TAG, "FDN handshake complete - routing to KonamiMetaGame (app 9)");
        LOG_I(TAG, "Challenge: %s, reward: %s",
               getGameDisplayName(pendingGameType),
               getKonamiButtonName(pendingReward));

        // Launch KonamiMetaGame app - it will handle all routing via Handshake state
        gameLaunched = true;
        PDN->setActiveApp(StateId(9));  // KONAMI_METAGAME_APP_ID
        return;
    }

    if (timeoutTimer.expired()) {
        LOG_W(TAG, "FDN handshake timed out - connection lost");
        transitionToConnectionLostState = true;
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
    snapshot->gameLaunched = gameLaunched;
    return snapshot;
}

void FdnDetected::onStateResumed(Device* PDN, Snapshot* snapshot) {
    if (snapshot) {
        auto* fdnSnapshot = static_cast<FdnDetectedSnapshot*>(snapshot);
        pendingGameType = fdnSnapshot->gameType;
        pendingReward = fdnSnapshot->reward;
        handshakeComplete = fdnSnapshot->handshakeComplete;
        gameLaunched = fdnSnapshot->gameLaunched;
    }

    // If we launched a game and it's now done, transition to FdnComplete
    if (gameLaunched) {
        transitionToFdnCompleteState = true;
    }
}

bool FdnDetected::transitionToIdle() {
    return transitionToIdleState;
}

bool FdnDetected::transitionToFdnComplete() {
    return transitionToFdnCompleteState;
}

bool FdnDetected::transitionToReencounter() {
    return transitionToReencounterState;
}

bool FdnDetected::transitionToKonamiPuzzle() {
    return transitionToKonamiPuzzleState;
}

bool FdnDetected::transitionToConnectionLost() {
    return transitionToConnectionLostState;
}
