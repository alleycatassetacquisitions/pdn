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
    pdn->getDisplay()->invalidateScreen();

    pdn->getDisplay()->setGlyphMode(FontMode::TEXT)->drawText("Demo Mode", 3, 16);

    if (demoGameplaySelected) {
        pdn->getDisplay()->drawButton("PLAY", 64, 36)->drawText("TEST", 25, 56);
    } else {
        pdn->getDisplay()->drawText("PLAY", 25, 36)->drawButton("TEST", 64, 56);
    }

    pdn->getDisplay()->render();
}
