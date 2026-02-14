#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
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

    auto& config = game->getConfig();
    auto& session = game->getSession();

    // Determine hard mode: tight budget on long grid
    bool isHard = (config.gridSize >= 10 && config.moveBudget <= 14);

    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "PATH DECODED (score=%d, hard=%d)", session.score, isHard);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("PATH DECODED", 10, 30);
    PDN->getDisplay()->render();

    // Win LED animation
    AnimationConfig animConfig;
    animConfig.type = AnimationType::IDLE;
    animConfig.speed = 20;
    animConfig.curve = EaseCurve::LINEAR;
    animConfig.initialState = CIPHER_PATH_WIN_STATE;
    animConfig.loopDelayMs = 0;
    animConfig.loop = true;
    PDN->getLightManager()->startAnimation(animConfig);

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

bool CipherPathWin::isTerminalState() const {
    return game->getConfig().managedMode;
}
