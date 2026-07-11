#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutProposal::ShootoutProposal(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_PROPOSAL)
    , ShootoutAwareState(ctx.shootoutManager, ctx.chainDuelManager) {}

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
    tickAbortGuard();
    ShootoutManager::Phase p = shootoutManager->getPhase();
    if (p == ShootoutManager::Phase::BRACKET_REVEAL) shouldGoToReveal_ = true;
    if (p == ShootoutManager::Phase::ABORTED) shouldGoToAborted_ = true;
}

void ShootoutProposal::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    shouldGoToReveal_ = false;
    shouldGoToAborted_ = false;
    resetAbortGuard();
}

bool ShootoutProposal::transitionToBracketReveal() { return shouldGoToReveal_; }
bool ShootoutProposal::transitionToAborted() { return shouldGoToAborted_; }
