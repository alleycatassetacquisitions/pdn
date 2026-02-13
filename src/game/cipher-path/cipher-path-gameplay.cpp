#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "CipherPathGameplay";

CipherPathGameplay::CipherPathGameplay(CipherPath* game) : State(CIPHER_GAMEPLAY) {
    this->game = game;
}

CipherPathGameplay::~CipherPathGameplay() {
    game = nullptr;
}

void CipherPathGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    needsEvaluation = false;
    displayIsDirty = true;

    LOG_I(TAG, "Gameplay started");

    // LED chase animation during gameplay
    AnimationConfig config;
    config.type = AnimationType::VERTICAL_CHASE;
    config.speed = 20;
    config.curve = EaseCurve::LINEAR;
    config.initialState = CIPHER_PATH_GAMEPLAY_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Set up button callbacks — UP (primary) and DOWN (secondary)
    parameterizedCallbackFunction upCallback = [](void* ctx) {
        auto* state = static_cast<CipherPathGameplay*>(ctx);
        auto* gm = state->game;
        auto& sess = gm->getSession();
        auto& cfg = gm->getConfig();

        // Player chose UP
        if (sess.cipher[sess.playerPosition] == 0) {
            // Correct — advance position
            sess.playerPosition++;
            sess.lastMoveCorrect = true;
        } else {
            // Wrong — waste a move
            sess.lastMoveCorrect = false;
        }
        sess.movesUsed++;

        // Check end conditions
        if (sess.playerPosition >= cfg.gridSize - 1 || sess.movesUsed >= cfg.moveBudget) {
            state->setNeedsEvaluation();
        }
        state->displayIsDirty = true;
    };

    parameterizedCallbackFunction downCallback = [](void* ctx) {
        auto* state = static_cast<CipherPathGameplay*>(ctx);
        auto* gm = state->game;
        auto& sess = gm->getSession();
        auto& cfg = gm->getConfig();

        // Player chose DOWN
        if (sess.cipher[sess.playerPosition] == 1) {
            // Correct — advance position
            sess.playerPosition++;
            sess.lastMoveCorrect = true;
        } else {
            // Wrong — waste a move
            sess.lastMoveCorrect = false;
        }
        sess.movesUsed++;

        // Check end conditions
        if (sess.playerPosition >= cfg.gridSize - 1 || sess.movesUsed >= cfg.moveBudget) {
            state->setNeedsEvaluation();
        }
        state->displayIsDirty = true;
    };

    PDN->getPrimaryButton()->setButtonPress(upCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(downCallback, this, ButtonInteraction::CLICK);

    renderGameplayScreen(PDN);
}

void CipherPathGameplay::onStateLoop(Device* PDN) {
    if (needsEvaluation) {
        transitionToEvaluateState = true;
        return;
    }

    if (displayIsDirty) {
        renderGameplayScreen(PDN);
        displayIsDirty = false;
    }
}

void CipherPathGameplay::onStateDismounted(Device* PDN) {
    transitionToEvaluateState = false;
    needsEvaluation = false;
    displayIsDirty = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool CipherPathGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}

void CipherPathGameplay::renderGameplayScreen(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    std::string posText = "Pos: " + std::to_string(session.playerPosition)
                        + "/" + std::to_string(config.gridSize - 1);
    std::string moveText = "Moves: " + std::to_string(session.movesUsed)
                         + "/" + std::to_string(config.moveBudget);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(posText.c_str(), 10, 20)
        ->drawText(moveText.c_str(), 10, 40);

    // Show feedback for last move
    if (session.movesUsed > 0) {
        const char* feedback = session.lastMoveCorrect ? ">> CORRECT" : ">> WRONG";
        PDN->getDisplay()->drawText(feedback, 10, 58);
    }

    PDN->getDisplay()->render();
}
