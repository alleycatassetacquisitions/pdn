#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutEliminated::ShootoutEliminated(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_ELIMINATED)
    , shootout_(ctx.shootoutManager) {}

void ShootoutEliminated::onStateMounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getLightManager()->stopAnimation();
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("OUT", 20);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("spectating", 50);
    d->render();
}

void ShootoutEliminated::onStateLoop(PDN* pdn) {
    shootout_->sync();
    auto p = shootout_->getPhase();
    if (p == ShootoutManager::Phase::ENDED) shouldGoToFinalStandings_ = true;
    if (p == ShootoutManager::Phase::ABORTED) shouldGoToAborted_ = true;
}

void ShootoutEliminated::onStateDismounted(PDN* pdn) {
    shouldGoToFinalStandings_ = false;
    shouldGoToAborted_ = false;
}

bool ShootoutEliminated::transitionToFinalStandings() { return shouldGoToFinalStandings_; }
bool ShootoutEliminated::transitionToAborted() { return shouldGoToAborted_; }
