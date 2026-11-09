#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "REGISTRATION"

RegistrationState::RegistrationState(FdnProvisioningManager* fdnProvisioningManager,
                                     RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::REGISTRATION)
    , fdnProvisioningManager(fdnProvisioningManager) {}

RegistrationState::~RegistrationState() { fdnProvisioningManager = nullptr; }

void RegistrationState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted — unprovisioned PDN cabled");
    screen = Screen::WAITING;
    playerName.clear();
    fdnProvisioningManager->setOnLookupDone([this](const char* name) {
        screen = name != nullptr ? Screen::WELCOME : Screen::FAILED;
        playerName = name != nullptr ? name : "";
        resultTimer.setTimer(RESULT_DISPLAY_MS);
        displayIsDirty = true;
    });
    render(fdn);
}

void RegistrationState::onStateLoop(FDN* fdn) {
    if (screen == Screen::WAITING && fdnProvisioningManager->isLookupInFlight()) {
        screen = Screen::LOOKING_UP;
        displayIsDirty = true;
    }
    if (displayIsDirty) {
        render(fdn);
        displayIsDirty = false;
    }
}

void RegistrationState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    fdnProvisioningManager->setOnLookupDone(nullptr);
    resultTimer.invalidate();
    displayIsDirty = false;
    screen = Screen::WAITING;
    playerName.clear();
}

bool RegistrationState::transitionToIdle() {
    if (!isConnected()) return true;
    if (screen == Screen::WELCOME || screen == Screen::FAILED) return resultTimer.expired();
    return screen == Screen::WAITING && !fdnProvisioningManager->hasUnprovisionedPdnCabled();
}

void RegistrationState::render(FDN* fdn) {
    const char* top = "REGISTER";
    const char* bottom = "ENTER CODE";
    if (screen == Screen::LOOKING_UP) {
        top = "LOOKING";
        bottom = "UP";
    } else if (screen == Screen::WELCOME) {
        top = "WELCOME";
        bottom = playerName.c_str();
    } else if (screen == Screen::FAILED) {
        top = "UNABLE TO";
        bottom = "LOCATE ASSET";
    }
    fdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText(top, centeredTextX(top), 28)
    ->drawText(bottom, centeredTextX(bottom), 44)
    ->render();
}
