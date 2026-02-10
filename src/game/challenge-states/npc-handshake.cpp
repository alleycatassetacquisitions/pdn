#include "game/challenge-states.hpp"
#include "game/challenge-game.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NpcHandshake";

NpcHandshake::NpcHandshake(ChallengeGame* game) :
    State(ChallengeStateId::NPC_HANDSHAKE),
    game(game)
{
}

NpcHandshake::~NpcHandshake() {
    game = nullptr;
}

void NpcHandshake::onStateMounted(Device* PDN) {
    LOG_I(TAG, "NPC Handshake mounted");

    transitionToGameActiveState = false;
    transitionToIdleState = false;
    playerMacAddress.clear();

    timeoutTimer.setTimer(HANDSHAKE_TIMEOUT_MS);

    // Read the MAC address that triggered this transition
    // The Idle state received "smac<MAC>" — we need to read it from serial
    // Actually, the serial callback in Idle already consumed it.
    // We re-read from the serial buffer in case there's more data.
    PDN->setOnStringReceivedCallback([this, PDN](const std::string& message) {
        LOG_I(TAG, "Handshake serial: %s", message.c_str());
        if (message.rfind(SEND_MAC_ADDRESS, 0) == 0) {
            playerMacAddress = message.substr(SEND_MAC_ADDRESS.length());
            LOG_I(TAG, "Player MAC: %s", playerMacAddress.c_str());

            // NOTE: ESP-NOW peer registration deferred to Feature D.
            // WirelessManager doesn't expose addPeer directly;
            // the ESP-NOW driver handles it when sending data.

            // Send acknowledgment
            PDN->writeString(CHALLENGE_ACK.c_str());

            transitionToGameActiveState = true;
        }
    });

    // Display connecting message
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("CONNECTING...", 10, 30);
    PDN->getDisplay()->render();
}

void NpcHandshake::onStateLoop(Device* PDN) {
    timeoutTimer.updateTime();
    if (timeoutTimer.expired()) {
        LOG_W(TAG, "Handshake timed out");
        transitionToIdleState = true;
    }
}

void NpcHandshake::onStateDismounted(Device* PDN) {
    timeoutTimer.invalidate();
    PDN->clearCallbacks();
}

bool NpcHandshake::transitionToGameActive() {
    return transitionToGameActiveState;
}

bool NpcHandshake::transitionToIdle() {
    return transitionToIdleState;
}
