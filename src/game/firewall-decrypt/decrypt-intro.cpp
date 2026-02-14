#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "DecryptIntro";

DecryptIntro::DecryptIntro(FirewallDecrypt* game) : State(DECRYPT_INTRO) {
    this->game = game;
}

DecryptIntro::~DecryptIntro() {
    game = nullptr;
}

void DecryptIntro::onStateMounted(Device* PDN) {
    transitionToScanState = false;

    LOG_I(TAG, "Firewall Decrypt intro");

    // Reset game state for fresh play
    game->resetGame();
    game->setStartTime(SimpleTimer::getPlatformClock()->milliseconds());
    game->seedRng();
    game->setupRound();

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("FIREWALL", 20, 20)
        ->drawText("DECRYPT", 25, 40);
    PDN->getDisplay()->render();

    // Green idle animation
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = FIREWALL_DECRYPT_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    introTimer.setTimer(INTRO_DURATION_MS);
}

void DecryptIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToScanState = true;
    }
}

void DecryptIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToScanState = false;
}

bool DecryptIntro::transitionToScan() {
    return transitionToScanState;
}
