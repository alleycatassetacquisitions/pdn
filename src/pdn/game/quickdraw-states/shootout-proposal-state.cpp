#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutProposal::ShootoutProposal(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_PROPOSAL)
    , LoopBreakAbortState(ctx.shootoutManager)
    , chainDuelManager_(ctx.chainDuelManager) {}

void ShootoutProposal::onStateMounted(PDN* pdn) {
    shootoutManager->startProposal();
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("SHOOTOUT", 15);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("press to", 40);
    d->drawCenteredText("confirm", 55);
    d->render();

    parameterizedCallbackFunction confirmCb = [](void* ctx) {
        static_cast<ShootoutProposal*>(ctx)->shootoutManager->confirmLocal();
    };
    pdn->getPrimaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);
}

void ShootoutProposal::onStateLoop(PDN* pdn) {
    shootoutManager->sync();
    auto p = shootoutManager->getPhase();
    if (p == ShootoutManager::Phase::BRACKET_REVEAL) shouldGoToReveal_ = true;
    if (p == ShootoutManager::Phase::ABORTED) shouldGoToAborted_ = true;
    if (p == ShootoutManager::Phase::IDLE) shouldGoToIdle_ = true;
    bool loopBroken = chainDuelManager_ && !chainDuelManager_->isLoop();
    if (tickAbortGuard(loopBroken)) shouldGoToAborted_ = true;
}

void ShootoutProposal::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    shouldGoToReveal_ = false;
    shouldGoToIdle_ = false;
    shouldGoToAborted_ = false;
    resetAbortGuard();
}

bool ShootoutProposal::transitionToBracketReveal() { return shouldGoToReveal_; }
bool ShootoutProposal::transitionToIdle() { return shouldGoToIdle_; }
bool ShootoutProposal::transitionToAborted() { return shouldGoToAborted_; }
