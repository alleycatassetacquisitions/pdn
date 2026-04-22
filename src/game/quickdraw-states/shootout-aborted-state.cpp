#include "game/quickdraw-states.hpp"
#include "device/device.hpp"

ShootoutAborted::ShootoutAborted(ShootoutManager* shootout)
    : State(SHOOTOUT_ABORTED), shootout_(shootout) {}

void ShootoutAborted::onStateMounted(Device *PDN) {
    displayTimer_.setTimer(ABORTED_DISPLAY_MS);
    auto* d = PDN->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("ABORTED", 30);
    d->render();
}

void ShootoutAborted::onStateLoop(Device *PDN) {
    if (displayTimer_.expired()) {
        shouldGoToIdle_ = true;
    }
}

void ShootoutAborted::onStateDismounted(Device *PDN) {
    if (shootout_) shootout_->resetToIdle();
    displayTimer_.invalidate();
    shouldGoToIdle_ = false;
}

bool ShootoutAborted::transitionToIdle() { return shouldGoToIdle_; }
