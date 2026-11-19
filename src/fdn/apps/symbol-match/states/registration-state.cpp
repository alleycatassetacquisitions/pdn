#include "apps/symbol-match/symbol-match-states.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "Registration";

Registration::Registration(FdnProvisioningManager* fdnProvisioningManager)
    : TypedState<FDN>(REGISTRATION)
    , fdnProvisioningManager(fdnProvisioningManager) {}

Registration::~Registration() { fdnProvisioningManager = nullptr; }

void Registration::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted");
    fdn->getLightManager()->stopAnimation();
    fdnProvisioningManager->setRegistrationScreenShown(true);
    fdnProvisioningManager->setOnViewChanged([this](const RegistrationView& changed) {
        view = changed;
        displayIsDirty = true;
        transitionToSelectionState = changed.screen == RegistrationScreen::NONE;
    });
}

void Registration::onStateLoop(FDN* fdn) {
    if (!displayIsDirty) return;
    displayIsDirty = false;
    const RegistrationScreenLines lines = getRegistrationScreenLines(view);
    fdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawCenteredText(lines.top, 28)
    ->drawCenteredText(lines.bottom, 44)
    ->render();
}

void Registration::onStateDismounted(FDN* fdn) {
    fdnProvisioningManager->setOnViewChanged(nullptr);
    fdnProvisioningManager->setRegistrationScreenShown(false);
    fdn->getDisplay()->invalidateScreen();
    view = RegistrationView{};
    displayIsDirty = false;
    transitionToSelectionState = false;
    LOG_I(TAG, "Dismounted");
}

bool Registration::transitionToSelection() { return transitionToSelectionState; }
