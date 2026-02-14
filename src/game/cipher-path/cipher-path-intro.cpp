#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "CipherPathIntro";

CipherPathIntro::CipherPathIntro(CipherPath* game) : State(CIPHER_INTRO) {
    this->game = game;
}

CipherPathIntro::~CipherPathIntro() {
    game = nullptr;
}

void CipherPathIntro::onStateMounted(Device* PDN) {
    transitionToShowState = false;

    LOG_I(TAG, "Cipher Path intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();
    game->setStartTime(SimpleTimer::getPlatformClock()->milliseconds());
    game->seedRng();

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("CIPHER PATH", 10, 20)
        ->drawText("Decode the route.", 10, 45);
    PDN->getDisplay()->render();

    // Start idle LED animation
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = CIPHER_PATH_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Start intro timer
    introTimer.setTimer(INTRO_DURATION_MS);
}

void CipherPathIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToShowState = true;
    }
}

void CipherPathIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToShowState = false;
}

bool CipherPathIntro::transitionToShow() {
    return transitionToShowState;
}
