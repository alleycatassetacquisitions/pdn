#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "SpikeVectorGameplay";

SpikeVectorGameplay::SpikeVectorGameplay(SpikeVector* game) : State(SPIKE_GAMEPLAY) {
    this->game = game;
}

SpikeVectorGameplay::~SpikeVectorGameplay() {
    game = nullptr;
}

void SpikeVectorGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;

    LOG_I(TAG, "Gameplay started, gap at %d", game->getSession().gapPosition);

    // Set up button callbacks for cursor movement
    parameterizedCallbackFunction upCallback = [](void* ctx) {
        auto* state = static_cast<SpikeVectorGameplay*>(ctx);
        auto& sess = state->game->getSession();
        if (sess.cursorPosition > 0) sess.cursorPosition--;
    };

    parameterizedCallbackFunction downCallback = [](void* ctx) {
        auto* state = static_cast<SpikeVectorGameplay*>(ctx);
        auto& sess = state->game->getSession();
        if (sess.cursorPosition < state->game->getConfig().numPositions - 1) sess.cursorPosition++;
    };

    PDN->getPrimaryButton()->setButtonPress(upCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(downCallback, this, ButtonInteraction::CLICK);

    // Start LED chase animation
    AnimationConfig animConfig;
    animConfig.type = AnimationType::VERTICAL_CHASE;
    animConfig.speed = 8;
    animConfig.curve = EaseCurve::LINEAR;
    animConfig.initialState = SPIKE_VECTOR_GAMEPLAY_STATE;
    animConfig.loopDelayMs = 0;
    animConfig.loop = true;
    PDN->getLightManager()->startAnimation(animConfig);

    // Start wall advance timer
    wallTimer.setTimer(game->getConfig().approachSpeedMs);
}

void SpikeVectorGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Advance wall on timer
    if (wallTimer.expired()) {
        session.wallPosition++;

        if (session.wallPosition >= config.trackLength) {
            // Wall has arrived
            session.wallArrived = true;
            transitionToEvaluateState = true;
            return;
        }

        // Update display
        PDN->getDisplay()->invalidateScreen();

        std::string posStr = "Pos: " + std::to_string(session.cursorPosition);
        std::string wallStr = "Wall: " + std::to_string(session.wallPosition) +
                              "/" + std::to_string(config.trackLength);
        std::string gapStr = "Gap: " + std::to_string(session.gapPosition);

        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText(posStr.c_str(), 5, 15)
            ->drawText(wallStr.c_str(), 5, 35)
            ->drawText(gapStr.c_str(), 5, 55);
        PDN->getDisplay()->render();

        // Reset timer for next wall step
        wallTimer.setTimer(config.approachSpeedMs);
    }
}

void SpikeVectorGameplay::onStateDismounted(Device* PDN) {
    wallTimer.invalidate();
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool SpikeVectorGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}
