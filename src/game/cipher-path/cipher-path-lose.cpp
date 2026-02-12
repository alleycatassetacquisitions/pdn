#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "CipherPathLose";

CipherPathLose::CipherPathLose(CipherPath* game) : State(CIPHER_LOSE) {
    this->game = game;
}

CipherPathLose::~CipherPathLose() {
    game = nullptr;
}

void CipherPathLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = 0;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "PATH LOST");

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("PATH LOST", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(255);

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}

void CipherPathLose::onStateLoop(Device* PDN) {
    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void CipherPathLose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool CipherPathLose::transitionToIntro() {
    return transitionToIntroState;
}

bool CipherPathLose::isTerminalState() {
    return game->getConfig().managedMode;
}
