#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "CipherPathIntro";

CipherPathIntro::CipherPathIntro(CipherPath* game) : State(CIPHER_INTRO) {
    this->game = game;
}

CipherPathIntro::~CipherPathIntro() {
    game = nullptr;
}

void CipherPathIntro::onStateMounted(Device* PDN) {
    transitionToWinState = false;

    LOG_I(TAG, "Cipher Path intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("CIPHER PATH", 10, 20)
        ->drawText("Find the way.", 10, 45);
    PDN->getDisplay()->render();

    // Start intro timer — stub auto-wins after this
    introTimer.setTimer(INTRO_DURATION_MS);
}

void CipherPathIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToWinState = true;
    }
}

void CipherPathIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToWinState = false;
}

bool CipherPathIntro::transitionToWin() {
    return transitionToWinState;
}
