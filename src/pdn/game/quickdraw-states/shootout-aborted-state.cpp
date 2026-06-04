#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutAborted::ShootoutAborted(ShootoutManager* shootout)
    : TypedState<PDN>(SHOOTOUT_ABORTED), shootout_(shootout) {}

void ShootoutAborted::onStateMounted(PDN* pdn) {
    pdn->getLightManager()->stopAnimation();
    displayTimer_.setTimer(ABORTED_DISPLAY_MS);
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("ABORTED", 30);
    d->render();
}

void ShootoutAborted::onStateLoop(PDN* pdn) {
    if (displayTimer_.expired()) {
        shouldGoToIdle_ = true;
    }
}

void ShootoutAborted::onStateDismounted(PDN* pdn) {
    if (shootout_) shootout_->resetToIdle();
    displayTimer_.invalidate();
    shouldGoToIdle_ = false;
}

bool ShootoutAborted::transitionToIdle() { return shouldGoToIdle_; }
