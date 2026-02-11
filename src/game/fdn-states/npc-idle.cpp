#include "game/fdn-states.hpp"
#include "game/fdn-game.hpp"
#include "game/fdn-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NpcIdle";

NpcIdle::NpcIdle(FdnGame* game) :
    State(FdnStateId::NPC_IDLE),
    game(game)
{
}

NpcIdle::~NpcIdle() {
    game = nullptr;
}

void NpcIdle::onStateMounted(Device* PDN) {
    LOG_I(TAG, "NPC Idle mounted — game: %s", getGameDisplayName(game->getGameType()));

    transitionToHandshakeState = false;

    PDN->setOnStringReceivedCallback(
        std::bind(&NpcIdle::serialEventCallback, this, std::placeholders::_1));

    broadcastTimer.setTimer(BROADCAST_INTERVAL_MS);

    AnimationConfig config;
    config.type = AnimationType::TRANSMIT_BREATH;
    config.speed = 8;
    config.curve = EaseCurve::EASE_IN_OUT;
    config.loop = true;
    config.loopDelayMs = 500;
    PDN->getLightManager()->startAnimation(config);

    renderDisplay(PDN);
}

void NpcIdle::onStateLoop(Device* PDN) {
    broadcastTimer.updateTime();

    if (broadcastTimer.expired()) {
        std::string fdnMsg = FDN_DEVICE_ID +
            std::to_string(static_cast<int>(game->getGameType())) + ":" +
            std::to_string(static_cast<int>(game->getReward()));
        PDN->writeString(fdnMsg.c_str());
        broadcastTimer.setTimer(BROADCAST_INTERVAL_MS);
    }
}

void NpcIdle::onStateDismounted(Device* PDN) {
    broadcastTimer.invalidate();
    PDN->clearCallbacks();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool NpcIdle::transitionToHandshake() {
    return transitionToHandshakeState;
}

void NpcIdle::serialEventCallback(const std::string& message) {
    LOG_I(TAG, "Serial received: %s", message.c_str());
    if (message.rfind(SEND_MAC_ADDRESS, 0) == 0) {
        transitionToHandshakeState = true;
    }
}

void NpcIdle::renderDisplay(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText(getGameDisplayName(game->getGameType()), 10, 20);

    std::string unlockText = std::string("Unlock: ") + getKonamiButtonName(game->getReward());
    PDN->getDisplay()->drawText(unlockText.c_str(), 10, 45);
    PDN->getDisplay()->render();
}
