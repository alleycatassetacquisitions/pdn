#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "state/state-machine-manager.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include "device/device-constants.hpp"
#include "device/device-types.hpp"
#include <cstdint>

static const char* TAG = "ChallengeDetected";

ChallengeDetected::ChallengeDetected(Player* player, StateMachineManager* smManager) :
    State(CHALLENGE_DETECTED),
    player(player),
    smManager(smManager)
{
}

ChallengeDetected::~ChallengeDetected() {
    player = nullptr;
    smManager = nullptr;
}

void ChallengeDetected::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    cackReceived = false;
    macSent = false;

    // Read the pending cdev message from Player (set by Idle)
    cdevMessage = player->getPendingCdevMessage();

    LOG_I(TAG, "Challenge detected, cdev: %s", cdevMessage.c_str());

    // Parse the cdev message
    if (!parseCdevMessage(cdevMessage, pendingGameType, pendingReward)) {
        LOG_W(TAG, "Failed to parse cdev message: %s", cdevMessage.c_str());
        transitionToIdleState = true;
        return;
    }

    LOG_I(TAG, "Challenge: %s, reward: %s",
           getGameDisplayName(pendingGameType),
           getKonamiButtonName(pendingReward));

    // Set up serial callback to listen for cack
    PDN->setOnStringReceivedCallback([this](const std::string& message) {
        LOG_I(TAG, "Serial received: %s", message.c_str());
        if (message == CHALLENGE_ACK) {
            LOG_I(TAG, "Received cack from NPC");
            cackReceived = true;
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

void ChallengeDetected::onStateLoop(Device* PDN) {
    timeoutTimer.updateTime();

    if (cackReceived) {
        LOG_I(TAG, "cack received, loading minigame");

        // Create the minigame based on game type
        MiniGame* miniGame = nullptr;

        if (pendingGameType == GameType::SIGNAL_ECHO) {
            SignalEchoConfig config = SIGNAL_ECHO_HARD;
            config.managedMode = true;
            miniGame = new SignalEcho(PDN, config);
        }
        // Future games: else if (pendingGameType == GameType::GHOST_RUNNER) { ... }

        if (miniGame && smManager) {
            // CHALLENGE_COMPLETE is at stateMap index 22
            smManager->pauseAndLoad(miniGame, 22);
        } else {
            LOG_W(TAG, "Failed to create minigame or no SM Manager");
            delete miniGame;
            transitionToIdleState = true;
        }
        return;
    }

    if (timeoutTimer.expired()) {
        LOG_W(TAG, "Challenge handshake timed out");
        transitionToIdleState = true;
    }
}

void ChallengeDetected::onStateDismounted(Device* PDN) {
    timeoutTimer.invalidate();
    cdevMessage.clear();
    cackReceived = false;
    macSent = false;
    player->clearPendingChallenge();
    PDN->clearCallbacks();
}

bool ChallengeDetected::transitionToIdle() {
    return transitionToIdleState;
}
