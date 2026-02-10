#include "game/challenge-states.hpp"
#include "game/challenge-game.hpp"
#include "game/challenge-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NpcIdle";

NpcIdle::NpcIdle(ChallengeGame* game) :
    State(ChallengeStateId::NPC_IDLE),
    game(game)
{
}

NpcIdle::~NpcIdle() {
    game = nullptr;
}

void NpcIdle::onStateMounted(Device* PDN) {
    LOG_I(TAG, "NPC Idle mounted — game: %s", getGameDisplayName(game->getGameType()));

    transitionToHandshakeState = false;

    // Set serial callback for incoming MAC address
    PDN->setOnStringReceivedCallback(
        std::bind(&NpcIdle::serialEventCallback, this, std::placeholders::_1));

    // Start broadcast timer
    broadcastTimer.setTimer(BROADCAST_INTERVAL_MS);

    // Set up LED animation (reuse TRANSMIT_BREATH for slow pulse)
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
        // Broadcast challenge device identification
        std::string cdevMsg = CHALLENGE_DEVICE_ID +
            std::to_string(static_cast<int>(game->getGameType())) + ":" +
            std::to_string(static_cast<int>(game->getReward()));
        PDN->writeString(cdevMsg.c_str());
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
        // Player sent their MAC address — begin handshake
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
