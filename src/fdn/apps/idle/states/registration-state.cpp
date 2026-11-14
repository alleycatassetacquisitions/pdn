#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "REGISTRATION"

RegistrationState::RegistrationState(FdnProvisioningManager* fdnProvisioningManager,
                                     RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::REGISTRATION)
    , fdnProvisioningManager(fdnProvisioningManager) {}

RegistrationState::~RegistrationState() {
    fdnProvisioningManager->setOnLookupDone(nullptr);
    fdnProvisioningManager->setLookupDuringPlayAllowed(false);
    fdnProvisioningManager = nullptr;
}

void RegistrationState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted — unprovisioned PDN cabled");
    fdnProvisioningManager->setLookupDuringPlayAllowed(true);
    fdnProvisioningManager->setOnLookupDone([this](ProvisionLookupResult result, const char* name) {
        if (result == ProvisionLookupResult::PROFILE_DELIVERED) {
            screen = Screen::WELCOME;
        } else if (result == ProvisionLookupResult::KEY_NOT_ISSUED) {
            screen = Screen::KEY_NOT_ISSUED;
        } else if (result == ProvisionLookupResult::SERVER_NOT_TRUSTED) {
            screen = Screen::SERVER_NOT_TRUSTED;
        } else {
            screen = Screen::FAILED;
        }
        playerName = name != nullptr ? name : "";
        resultTimer.setTimer(RESULT_DISPLAY_MS);
        displayIsDirty = true;
    });
    render(fdn);
}

void RegistrationState::onStateLoop(FDN* fdn) {
    // A second PDN's lookup can start while the first PDN's result is still up;
    // moving to LOOKING_UP stops the result timer from dismounting on top of it.
    if (screen != Screen::LOOKING_UP && fdnProvisioningManager->isLookupInFlight()) {
        screen = Screen::LOOKING_UP;
        resultTimer.invalidate();
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
    fdnProvisioningManager->setLookupDuringPlayAllowed(false);
    // On the unplug exit a lookup can still be in flight; ending it here returns
    // the radio to ESP-NOW instead of waiting out the deadline.
    fdnProvisioningManager->cancelLookup();
    resultTimer.invalidate();
    displayIsDirty = false;
    screen = Screen::WAITING;
    playerName.clear();
}

bool RegistrationState::transitionToIdle() {
    if (!isConnected()) return true;
    if (fdnProvisioningManager->isLookupInFlight()) return false;
    if (screen == Screen::WELCOME || screen == Screen::FAILED || screen == Screen::KEY_NOT_ISSUED ||
        screen == Screen::SERVER_NOT_TRUSTED) {
        return resultTimer.expired();
    }
    return !fdnProvisioningManager->hasUnprovisionedPdnCabled();
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
    } else if (screen == Screen::KEY_NOT_ISSUED) {
        top = "UNABLE TO";
        bottom = "ISSUE KEY";
    } else if (screen == Screen::SERVER_NOT_TRUSTED) {
        top = "SERVER";
        bottom = "NOT TRUSTED";
    }
    fdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText(top, centeredTextX(top), 28)
    ->drawText(bottom, centeredTextX(bottom), 44)
    ->render();
}
