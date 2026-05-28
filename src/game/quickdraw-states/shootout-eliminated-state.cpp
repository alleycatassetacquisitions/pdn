#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutEliminated::ShootoutEliminated(ShootoutManager* shootout)
    : State(SHOOTOUT_ELIMINATED), shootout_(shootout) {}

void ShootoutEliminated::onStateMounted(Device *PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getLightManager()->stopAnimation();
    auto* d = PDN->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("OUT", 20);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("spectating", 50);
    d->render();
}

void ShootoutEliminated::onStateLoop(Device *PDN) {
    shootout_->sync();
    auto p = shootout_->getPhase();
    if (p == ShootoutManager::Phase::ENDED) shouldGoToFinalStandings_ = true;
    if (p == ShootoutManager::Phase::ABORTED) shouldGoToAborted_ = true;
}

void ShootoutEliminated::onStateDismounted(Device *PDN) {
    shouldGoToFinalStandings_ = false;
    shouldGoToAborted_ = false;
}

bool ShootoutEliminated::transitionToFinalStandings() { return shouldGoToFinalStandings_; }
bool ShootoutEliminated::transitionToAborted() { return shouldGoToAborted_; }
