#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
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

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "PATH LOST (score=%d)", session.score);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("PATH LOST", 10, 30);
    PDN->getDisplay()->render();

    // Lose LED animation
    AnimationConfig animConfig;
    animConfig.type = AnimationType::IDLE;
    animConfig.speed = 12;
    animConfig.curve = EaseCurve::LINEAR;
    animConfig.initialState = CIPHER_PATH_LOSE_STATE;
    animConfig.loopDelayMs = 0;
    animConfig.loop = true;
    PDN->getLightManager()->startAnimation(animConfig);

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

bool CipherPathLose::isTerminalState() const {
    return game->getConfig().managedMode;
}
