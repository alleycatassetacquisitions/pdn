#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutOutcome::ShootoutOutcome(QuickdrawStateId id, ShootoutManager* shootout,
                                 const char* heading, int headingY,
                                 const char* detail, int detailY,
                                 unsigned long dwellMs)
    : TypedState<PDN>(id), shootout_(shootout), heading_(heading), headingY_(headingY),
      detail_(detail), detailY_(detailY), dwellMs_(dwellMs) {}

void ShootoutOutcome::onStateMounted(PDN* pdn) {
    // Clear stale button callbacks (Duel leaves its press handler live when it
    // exits on a phase abort).
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getLightManager()->stopAnimation();
    if (dwellMs_ > 0) displayTimer_.setTimer(dwellMs_);
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText(heading_, headingY_);
    if (detail_ != nullptr) {
        d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
        d->drawCenteredText(detail_, detailY_);
    }
    d->render();
}

void ShootoutOutcome::onStateLoop(PDN* pdn) {
    auto p = shootout_->getPhase();
    if (p == ShootoutManager::Phase::ENDED) shouldGoToFinalStandings_ = true;
    if (p == ShootoutManager::Phase::ABORTED) shouldGoToAborted_ = true;
    if (displayTimer_.expired()) shouldGoToIdle_ = true;
}

void ShootoutOutcome::onStateDismounted(PDN* pdn) {
    // A dwell screen exits to Idle: clear the tournament so phase_ doesn't
    // stay set and block the Idle transitions (mirrors ShootoutFinalStandings).
    if (dwellMs_ > 0 && shootout_) shootout_->resetToIdle();
    displayTimer_.invalidate();
    shouldGoToFinalStandings_ = false;
    shouldGoToAborted_ = false;
    shouldGoToIdle_ = false;
}

bool ShootoutOutcome::transitionToFinalStandings() { return shouldGoToFinalStandings_; }
bool ShootoutOutcome::transitionToAborted() { return shouldGoToAborted_; }
bool ShootoutOutcome::transitionToIdle() { return shouldGoToIdle_; }
