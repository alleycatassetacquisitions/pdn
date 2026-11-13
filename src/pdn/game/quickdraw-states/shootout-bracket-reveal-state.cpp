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

    phaseSubscription.open(shootoutManager, [this](ShootoutManager::Phase phase) {
        const bool matchStarted = phase == ShootoutManager::Phase::MATCH_IN_PROGRESS;
        const bool playing = matchStarted && shootoutManager->isInCurrentMatch();
        transitionToMatchCountdownState = playing;
        transitionToSpectatorState = matchStarted && !playing;
    });
}

void ShootoutBracketReveal::onStateDismounted(PDN* pdn) {
    phaseSubscription.close();
    transitionToMatchCountdownState = false;
    transitionToSpectatorState = false;
}

bool ShootoutBracketReveal::transitionToMatchCountdown() const {
    return transitionToMatchCountdownState;
}

bool ShootoutBracketReveal::transitionToSpectator() const { return transitionToSpectatorState; }
