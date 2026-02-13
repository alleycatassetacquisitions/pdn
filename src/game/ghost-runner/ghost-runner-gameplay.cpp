#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "GhostRunnerGameplay";

GhostRunnerGameplay::GhostRunnerGameplay(GhostRunner* game) : State(GHOST_GAMEPLAY) {
    this->game = game;
}

GhostRunnerGameplay::~GhostRunnerGameplay() {
    game = nullptr;
}

void GhostRunnerGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;

    auto& session = game->getSession();
    session.ghostPosition = 0;
    session.playerPressed = false;

    LOG_I(TAG, "Gameplay started, ghost speed %dms", game->getConfig().ghostSpeedMs);

    // Set up PRIMARY button callback for player press
    parameterizedCallbackFunction pressCallback = [](void* ctx) {
        auto* state = static_cast<GhostRunnerGameplay*>(ctx);
        // Record press — don't transition here, let the loop handle it
        auto* gm = state->game;
        auto& sess = gm->getSession();
        if (!sess.playerPressed) {
            sess.playerPressed = true;
        }
    };

    PDN->getPrimaryButton()->setButtonPress(pressCallback, this, ButtonInteraction::CLICK);

    // Start chase animation
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 8;
    ledConfig.curve = EaseCurve::EASE_OUT;
    ledConfig.initialState = GHOST_RUNNER_GAMEPLAY_STATE;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Start first ghost step timer
    ghostStepTimer.setTimer(game->getConfig().ghostSpeedMs);
}

void GhostRunnerGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Check if player pressed
    if (session.playerPressed) {
        transitionToEvaluateState = true;
        return;
    }

    // Advance ghost position on timer
    if (ghostStepTimer.expired()) {
        session.ghostPosition++;

        // Update display with ghost position
        PDN->getDisplay()->invalidateScreen();

        // Draw target zone indicator
        std::string zoneStr = "[" + std::to_string(config.targetZoneStart) +
                              "-" + std::to_string(config.targetZoneEnd) + "]";
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("GHOST RUNNER", 10, 12);

        std::string posStr = "Pos: " + std::to_string(session.ghostPosition);
        PDN->getDisplay()->drawText(posStr.c_str(), 10, 35);
        PDN->getDisplay()->drawText(zoneStr.c_str(), 10, 50);
        PDN->getDisplay()->render();

        // Check timeout — ghost reached end of screen
        if (session.ghostPosition >= config.screenWidth) {
            transitionToEvaluateState = true;
            return;
        }

        // Restart step timer
        ghostStepTimer.setTimer(config.ghostSpeedMs);
    }
}

void GhostRunnerGameplay::onStateDismounted(Device* PDN) {
    ghostStepTimer.invalidate();
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
}

bool GhostRunnerGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}
