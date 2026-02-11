#include "game/fdn-states.hpp"
#include "game/fdn-game.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NpcHandshake";

NpcHandshake::NpcHandshake(FdnGame* game) :
    State(FdnStateId::NPC_HANDSHAKE),
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

    PDN->setOnStringReceivedCallback([this, PDN](const std::string& message) {
        LOG_I(TAG, "Handshake serial: %s", message.c_str());
        if (message.rfind(SEND_MAC_ADDRESS, 0) == 0) {
            playerMacAddress = message.substr(SEND_MAC_ADDRESS.length());
            LOG_I(TAG, "Player MAC: %s", playerMacAddress.c_str());

            PDN->writeString(FDN_ACK.c_str());

            transitionToGameActiveState = true;
        }
    });

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
