#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutProposal::ShootoutProposal(ShootoutManager* shootout, ChainDuelManager* chainDuelManager)
    : State(SHOOTOUT_PROPOSAL), shootout_(shootout), chainDuelManager_(chainDuelManager) {}

void ShootoutProposal::onStateMounted(Device *PDN) {
    shootout_->startProposal();
    auto* d = PDN->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("SHOOTOUT", 15);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("press to", 40);
    d->drawCenteredText("confirm", 55);
    d->render();

    parameterizedCallbackFunction confirmCb = [](void *ctx) {
        static_cast<ShootoutProposal*>(ctx)->shootout_->confirmLocal();
    };
    PDN->getPrimaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);
}

void ShootoutProposal::onStateLoop(Device *PDN) {
    shootout_->sync();
    auto p = shootout_->getPhase();
    if (p == ShootoutManager::Phase::BRACKET_REVEAL) shouldGoToReveal_ = true;
    if (p == ShootoutManager::Phase::IDLE || p == ShootoutManager::Phase::ABORTED) shouldGoToIdle_ = true;
    if (chainDuelManager_ && !chainDuelManager_->isLoop()) {
        shootout_->resetToIdle();
        shouldGoToIdle_ = true;
    }
}

void ShootoutProposal::onStateDismounted(Device *PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    shouldGoToReveal_ = false;
    shouldGoToIdle_ = false;
}

bool ShootoutProposal::transitionToBracketReveal() { return shouldGoToReveal_; }
bool ShootoutProposal::transitionToIdle() { return shouldGoToIdle_; }
