#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "REGISTRATION"

namespace {

// The primary jack (Serial2) is JACK 1, the secondary JACK 2, as labelled on the FDN.
const char* getJackLabel(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK_SECONDARY ? "JACK 2" : "JACK 1";
}

struct ScreenLines {
    const char* top;
    const char* bottom;
};

ScreenLines getResultLines(ProvisionLookupResult result, const char* playerName,
                           SerialIdentifier jack) {
    switch (result) {
        case ProvisionLookupResult::PROFILE_DELIVERED: return {"WELCOME", playerName};
        case ProvisionLookupResult::NO_PLAYER: return {"SEE DESK", getJackLabel(jack)};
        case ProvisionLookupResult::SERVER_UNREACHABLE: return {"SERVER", "UNREACHABLE"};
        case ProvisionLookupResult::SERVER_NOT_TRUSTED: return {"SERVER", "NOT TRUSTED"};
        case ProvisionLookupResult::KEY_NOT_ISSUED: return {"UNABLE TO", "ISSUE KEY"};
        case ProvisionLookupResult::LOOKUP_FAILED: break;
    }
    return {"UNABLE TO", "LOCATE ASSET"};
}

}  // namespace

RegistrationState::RegistrationState(FdnProvisioningManager* fdnProvisioningManager,
                                     RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::REGISTRATION)
    , fdnProvisioningManager(fdnProvisioningManager) {}

RegistrationState::~RegistrationState() {
    fdnProvisioningManager->setOnLookupDone(nullptr);
    fdnProvisioningManager->setRegistrationScreenShown(false);
    fdnProvisioningManager = nullptr;
}

void RegistrationState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted — registration queued");
    fdnProvisioningManager->setRegistrationScreenShown(true);
    fdnProvisioningManager->setOnLookupDone(
    [this](ProvisionLookupResult result, SerialIdentifier jack, const char* name) {
        screen = Screen::RESULT;
        lookupResult = result;
        screenJack = jack;
        playerName = name != nullptr ? name : "";
        displayIsDirty = true;
    });
    showPendingRegistration();
    render(fdn);
    displayIsDirty = false;
}

void RegistrationState::onStateLoop(FDN* fdn) {
    if (fdnProvisioningManager->isLookupInFlight()) {
        if (screen != Screen::LOOKING_UP) {
            screen = Screen::LOOKING_UP;
            displayIsDirty = true;
        }
    } else {
        // Nothing is pending while the manager holds a lookup's result, so the
        // result stays up for its full time before the next prompt replaces it.
        showPendingRegistration();
    }
    if (displayIsDirty) {
        render(fdn);
        displayIsDirty = false;
    }
}

void RegistrationState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    fdnProvisioningManager->setOnLookupDone(nullptr);
    fdnProvisioningManager->setRegistrationScreenShown(false);
    // On the unplug exit a lookup can still be in flight; ending it here returns
    // the radio to ESP-NOW instead of waiting out the deadline.
    fdnProvisioningManager->cancelLookup();
    displayIsDirty = false;
    screen = Screen::WAITING;
    lookupResult = ProvisionLookupResult::LOOKUP_FAILED;
    screenJack = SerialIdentifier::INPUT_JACK;
    playerName.clear();
}

bool RegistrationState::transitionToIdle() {
    if (!isConnected()) return true;
    if (fdnProvisioningManager->isLookupInFlight()) return false;
    if (screen == Screen::RESULT && fdnProvisioningManager->isHoldingLookupResult()) return false;
    return !fdnProvisioningManager->hasUnprovisionedPdnCabled();
}

// The reader is checked on every loop while a registration is pending, so a
// reader that comes back turns NO READER into TAP BAND without a remount.
void RegistrationState::showPendingRegistration() {
    if (!fdnProvisioningManager->hasPendingRegistration()) return;
    const Screen pendingScreen =
    fdnProvisioningManager->isBandReaderResponding() ? Screen::TAP_BAND : Screen::NO_READER;
    const SerialIdentifier pendingJack = fdnProvisioningManager->getPendingJack();
    if (screen == pendingScreen && screenJack == pendingJack) return;
    screen = pendingScreen;
    screenJack = pendingJack;
    displayIsDirty = true;
}

void RegistrationState::render(FDN* fdn) {
    ScreenLines lines = {"TAP BAND", ""};
    if (screen == Screen::TAP_BAND) {
        lines.bottom = getJackLabel(screenJack);
    } else if (screen == Screen::NO_READER) {
        lines = {"NO READER", getJackLabel(screenJack)};
    } else if (screen == Screen::LOOKING_UP) {
        lines = {"LOOKING", "UP"};
    } else if (screen == Screen::RESULT) {
        lines = getResultLines(lookupResult, playerName.c_str(), screenJack);
    }
    fdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText(lines.top, centeredTextX(lines.top), 28)
    ->drawText(lines.bottom, centeredTextX(lines.bottom), 44)
    ->render();
}
