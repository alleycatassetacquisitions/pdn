#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "CipherPathWin";

CipherPathWin::CipherPathWin(CipherPath* game) : State(CIPHER_WIN) {
    this->game = game;
}

CipherPathWin::~CipherPathWin() {
    game = nullptr;
}

void CipherPathWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    // Stub: always easy-mode win (hardMode = false)
    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = 100;
    winOutcome.hardMode = false;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "PATH DECODED");

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("PATH DECODED", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void CipherPathWin::onStateLoop(Device* PDN) {
    if (winTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            // In standalone mode, restart the game
            transitionToIntroState = true;
        } else {
            // In managed mode, return to the previous app (e.g. Quickdraw)
            PDN->returnToPreviousApp();
        }
    }
}

void CipherPathWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool CipherPathWin::transitionToIntro() {
    return transitionToIntroState;
}

bool CipherPathWin::isTerminalState() {
    return game->getConfig().managedMode;
}
