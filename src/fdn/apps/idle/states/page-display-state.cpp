#include "apps/idle/idle-states.hpp"

PageDisplayState::PageDisplayState(PageManager* pageManager)
    : TypedState<FDN>(IdleStateId::PAGE_DISPLAY)
    , pageManager(pageManager) {}

PageDisplayState::~PageDisplayState() {
    pageManager = nullptr;
}

void PageDisplayState::onStateMounted(FDN* fdn) {
    done = false;
    displayController.begin(fdn->getDisplay(), fdn->getPrimaryButton(), pageManager);
}

void PageDisplayState::onStateLoop(FDN* fdn) {
    (void)fdn;
    displayController.loop();
    if (displayController.isFinished()) {
        done = true;
    }
}

void PageDisplayState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    displayController.end();
    done = false;
}

bool PageDisplayState::transitionToIdle() {
    return done;
}
