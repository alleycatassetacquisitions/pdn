#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>

static const char* TAG = "DecryptLose";

DecryptLose::DecryptLose(FirewallDecrypt* game) : State(DECRYPT_LOSE) {
    this->game = game;
}

DecryptLose::~DecryptLose() {
    game = nullptr;
}

void DecryptLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "FIREWALL INTACT. Score: %d", session.score);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("FIREWALL", 20, 20)
        ->drawText("INTACT", 30, 40);

    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %d", session.score);
    PDN->getDisplay()->drawText(scoreStr, 30, 55);
    PDN->getDisplay()->render();

    // Red LED fade
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 8;
    config.curve = EaseCurve::LINEAR;
    config.initialState = FIREWALL_DECRYPT_LOSE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    PDN->getHaptics()->setIntensity(255);

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}

void DecryptLose::onStateLoop(Device* PDN) {
    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void DecryptLose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool DecryptLose::transitionToIntro() {
    return transitionToIntroState;
}

bool DecryptLose::isTerminalState() {
    return game->getConfig().managedMode;
}
