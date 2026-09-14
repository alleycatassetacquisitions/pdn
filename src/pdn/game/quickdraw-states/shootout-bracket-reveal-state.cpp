#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutBracketReveal::ShootoutBracketReveal(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_BRACKET_REVEAL)
    , shootoutManager(ctx.shootoutManager) {}

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
    ShootoutManager::Phase p = shootoutManager->getPhase();
    if (p == ShootoutManager::Phase::MATCH_IN_PROGRESS) {
        if (shootoutManager->isLocalDuelist()) {
            shouldGoToDuelCountdown_ = true;
        } else {
            shouldGoToSpectator_ = true;
        }
    }
}

void ShootoutBracketReveal::onStateDismounted(PDN* pdn) {
    shouldGoToDuelCountdown_ = false;
    shouldGoToSpectator_ = false;
}

bool ShootoutBracketReveal::transitionToDuelCountdown() { return shouldGoToDuelCountdown_; }
bool ShootoutBracketReveal::transitionToSpectator() { return shouldGoToSpectator_; }
