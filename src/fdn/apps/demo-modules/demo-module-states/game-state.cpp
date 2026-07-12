#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "GameState";
}

GameState::GameState(int* primaryScore, int* secondaryScore,
                     const std::string* primaryLabel, const std::string* secondaryLabel)
    : TypedState<FDN>(DemoModuleStateId::GAME)
    , primaryScore_(primaryScore)
    , secondaryScore_(secondaryScore)
    , primaryLabel_(primaryLabel)
    , secondaryLabel_(secondaryLabel) {}

GameState::~GameState() {}

void GameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    // Reset scores each time the game is entered.
    if (primaryScore_)   *primaryScore_   = 10;
    if (secondaryScore_) *secondaryScore_ = 0;
    transitionToScoringState = false;
    gameDurationTimer_.setTimer(kGameDurationMs);

    renderDemoStateLabel(fdn, "PLAY");

    // FDN primary button: P1 scores a point; secondary: P2 scores; tertiary: end game.
    parameterizedCallbackFunction onPrimary = [](void* ctx) {
        auto* gs = static_cast<GameState*>(ctx);
        if (gs->primaryScore_) (*gs->primaryScore_)++;
        LOG_W(TAG, "P1 score: %d", *gs->primaryScore_);
    };
    parameterizedCallbackFunction onSecondary = [](void* ctx) {
        auto* gs = static_cast<GameState*>(ctx);
        if (gs->secondaryScore_) (*gs->secondaryScore_)++;
        LOG_W(TAG, "P2 score: %d", *gs->secondaryScore_);
    };
    parameterizedCallbackFunction onTertiary = [](void* ctx) {
        static_cast<GameState*>(ctx)->transitionToScoringState = true;
    };

    fdn->getPrimaryButton()->setButtonPress(onPrimary,   this, ButtonInteraction::PRESS);
    fdn->getSecondaryButton()->setButtonPress(onSecondary, this, ButtonInteraction::PRESS);
    fdn->getTertiaryButton()->setButtonPress(onTertiary,  this, ButtonInteraction::PRESS);
}

void GameState::onStateLoop(FDN* fdn) {
    if (gameDurationTimer_.expired()) {
        transitionToScoringState = true;
    }

    renderDemoStateLabel(fdn, "PLAY");
}

void GameState::onStateDismounted(FDN* fdn) {
    gameDurationTimer_.invalidate();
    LOG_W(TAG, "Dismounted — P1=%d P2=%d",
          primaryScore_ ? *primaryScore_ : 0,
          secondaryScore_ ? *secondaryScore_ : 0);
    fdn->getPrimaryButton()->removeButtonCallbacks();
    fdn->getSecondaryButton()->removeButtonCallbacks();
    fdn->getTertiaryButton()->removeButtonCallbacks();
}

bool GameState::transitionToScoring() {
    return transitionToScoringState;
}

void GameState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }
    LOG_W(TAG, "ControllerCmd: button=%d interaction=%d",
          static_cast<int>(command.buttonId),
          static_cast<int>(command.interactionId));
    // PDN top = P1 point; PDN bottom = P2 point (controllable from PDN too).
    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON &&
        command.interactionId == ButtonInteraction::CLICK) {
        if (primaryScore_) (*primaryScore_)++;
        LOG_W(TAG, "PDN P1 point — score: %d", *primaryScore_);
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON &&
               command.interactionId == ButtonInteraction::CLICK) {
        if (secondaryScore_) (*secondaryScore_)++;
        LOG_W(TAG, "PDN P2 point — score: %d", *secondaryScore_);
    }
}
