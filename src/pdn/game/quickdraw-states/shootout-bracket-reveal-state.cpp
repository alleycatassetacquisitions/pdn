#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutBracketReveal::ShootoutBracketReveal(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_BRACKET_REVEAL)
    , LoopBreakAbortState(ctx.shootoutManager)
    , chainDuelManager_(ctx.chainDuelManager) {}

void ShootoutBracketReveal::onStateMounted(PDN* pdn) {
    // Clear stale button callbacks left by ShootoutProposal.
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("BRACKET", 20);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("ready...", 50);
    d->render();
}

void ShootoutBracketReveal::onStateLoop(PDN* pdn) {
    shootoutManager->sync();
    auto p = shootoutManager->getPhase();
    if (p == ShootoutManager::Phase::MATCH_IN_PROGRESS) {
        if (shootoutManager->isLocalDuelist()) {
            shouldGoToDuelCountdown_ = true;
        } else {
            shouldGoToSpectator_ = true;
        }
    }
    if (p == ShootoutManager::Phase::ABORTED) shouldGoToAborted_ = true;
    bool loopBroken = chainDuelManager_ && !chainDuelManager_->isLoop();
    if (tickAbortGuard(loopBroken)) shouldGoToAborted_ = true;
}

void ShootoutBracketReveal::onStateDismounted(PDN* pdn) {
    shouldGoToDuelCountdown_ = false;
    shouldGoToSpectator_ = false;
    shouldGoToAborted_ = false;
    resetAbortGuard();
}

bool ShootoutBracketReveal::transitionToDuelCountdown() { return shouldGoToDuelCountdown_; }
bool ShootoutBracketReveal::transitionToSpectator() { return shouldGoToSpectator_; }
bool ShootoutBracketReveal::transitionToAborted() { return shouldGoToAborted_; }
