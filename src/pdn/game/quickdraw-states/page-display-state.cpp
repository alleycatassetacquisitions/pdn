#include "game/quickdraw-states.hpp"

PageDisplay::PageDisplay(PageManager* pageManager)
    : TypedState<PDN>(PAGE_DISPLAY)
    , pageManager(pageManager) {}

PageDisplay::~PageDisplay() {
    pageManager = nullptr;
}

void PageDisplay::onStateMounted(PDN* pdn) {
    done = false;
    displayController.begin(pdn->getDisplay(), pdn->getPrimaryButton(), pageManager);
}

void PageDisplay::onStateLoop(PDN* pdn) {
    (void)pdn;
    displayController.loop();
    if (displayController.isFinished()) {
        done = true;
    }
}

void PageDisplay::onStateDismounted(PDN* pdn) {
    (void)pdn;
    displayController.end();
    done = false;
}

bool PageDisplay::transitionToIdle() {
    return done;
}
