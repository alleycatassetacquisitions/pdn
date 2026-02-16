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

    PlatformClock* clock = SimpleTimer::getPlatformClock();
    game->setStartTime(clock != nullptr ? clock->milliseconds() : 0);
    game->seedRng(game->getConfig().rngSeed);

    // Display title screen with new subtitle
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("CIPHER PATH", 10, 20)
        ->drawText("Route the signal.", 10, 45);
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

    // Start intro timer and animation timer
    introTimer.setTimer(INTRO_DURATION_MS);
    animTimer.setTimer(150);  // Flash circuit elements every 150ms
}

void CipherPathIntro::onStateLoop(Device* PDN) {
    // Simple circuit flash animation — random wire segments flash on/off
    if (animTimer.expired()) {
        // Draw a few random small wire segments to create "circuit board" effect
        PDN->getDisplay()->setDrawColor(1);
        for (int i = 0; i < 8; i++) {
            int x = 10 + (rand() % 100);
            int y = 8 + (rand() % 48);
            int w = 3 + (rand() % 8);
            bool horiz = (rand() % 2) == 0;
            if (horiz) {
                PDN->getDisplay()->drawBox(x, y, w, 1);
            } else {
                PDN->getDisplay()->drawBox(x, y, 1, w);
            }
        }
        PDN->getDisplay()->render();
        animTimer.setTimer(150);
    }

    if (introTimer.expired()) {
        transitionToShowState = true;
    }
}

void CipherPathIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    animTimer.invalidate();
    transitionToShowState = false;
}

bool CipherPathIntro::transitionToShow() {
    return transitionToShowState;
}
