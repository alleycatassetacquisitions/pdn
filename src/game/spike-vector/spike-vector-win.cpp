#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "SpikeVectorWin";

SpikeVectorWin::SpikeVectorWin(SpikeVector* game) : State(SPIKE_WIN) {
    this->game = game;
}

SpikeVectorWin::~SpikeVectorWin() {
    game = nullptr;
}

void SpikeVectorWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Determine hard mode based on config parameters
    bool isHard = (config.hitsAllowed <= 1 && config.numPositions >= 7);

    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "VECTOR CLEAR — Score: %d, Hard: %d", session.score, isHard);

    // Display victory screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("VECTOR CLEAR", 10, 25);

    std::string scoreStr = "Score: " + std::to_string(session.score);
    PDN->getDisplay()->drawText(scoreStr.c_str(), 30, 50);
    PDN->getDisplay()->render();

    // Victory LED animation
    AnimationConfig animConfig;
    animConfig.type = AnimationType::VERTICAL_CHASE;
    animConfig.speed = 5;
    animConfig.curve = EaseCurve::EASE_IN_OUT;
    animConfig.initialState = SPIKE_VECTOR_WIN_STATE;
    animConfig.loopDelayMs = 500;
    animConfig.loop = true;
    PDN->getLightManager()->startAnimation(animConfig);

    // Celebration haptic
    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void SpikeVectorWin::onStateLoop(Device* PDN) {
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

void SpikeVectorWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool SpikeVectorWin::transitionToIntro() {
    return transitionToIntroState;
}

bool SpikeVectorWin::isTerminalState() const {
    return game->getConfig().managedMode;
}
