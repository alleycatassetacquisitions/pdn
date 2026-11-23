#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"

DemoModeMenuState::DemoModeMenuState()
    : TypedState<PDN>(PlayerFetchStateId::DEMO_MODE_MENU) {}

DemoModeMenuState::~DemoModeMenuState() {}

void DemoModeMenuState::onStateMounted(PDN* pdn) {
    demoGameplaySelected = true;
    displayIsDirty = true;

    pdn->getPrimaryButton()->setButtonPress(
    [](void* ctx) {
        DemoModeMenuState* demoModeMenu = static_cast<DemoModeMenuState*>(ctx);
        demoModeMenu->demoGameplaySelected = !demoModeMenu->demoGameplaySelected;
        demoModeMenu->displayIsDirty = true;
    },
    this, ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress(
    [](void* ctx) {
        DemoModeMenuState* demoModeMenu = static_cast<DemoModeMenuState*>(ctx);
        if (demoModeMenu->demoGameplaySelected) {
            demoModeMenu->transitionToChooseTeamState = true;
        } else {
            demoModeMenu->transitionToDeviceTestState = true;
        }
    },
    this, ButtonInteraction::CLICK);
}

void DemoModeMenuState::onStateLoop(PDN* pdn) {
    if (displayIsDirty) {
        renderUi(pdn);
        displayIsDirty = false;
    }
}

void DemoModeMenuState::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    transitionToChooseTeamState = false;
    transitionToDeviceTestState = false;
    displayIsDirty = false;
    demoGameplaySelected = true;
}

bool DemoModeMenuState::transitionToChooseTeam() const { return transitionToChooseTeamState; }

bool DemoModeMenuState::transitionToDeviceTest() const { return transitionToDeviceTestState; }

void DemoModeMenuState::renderUi(PDN* pdn) const {
    Display* display = pdn->getDisplay();
    const int centerX = display->getWidth() / 2;

    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT)->drawCenteredText("Demo Mode", 16);

    if (demoGameplaySelected) {
        display->drawButton("PLAY", centerX, 36)->drawCenteredText("TEST", 56);
    } else {
        display->drawCenteredText("PLAY", 36)->drawButton("TEST", centerX, 56);
    }

    display->render();
}
