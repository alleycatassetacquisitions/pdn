#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/firmware-update-manager.hpp"

#include <algorithm>
#include <cstring>

AwaitBandState::AwaitBandState(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                               PlayerProvider* playerProvider, BandRegistration* bandRegistration,
                               FirmwareUpdateManager* firmwareUpdateManager)
    : ConnectState<PDN>(remoteDeviceCoordinator, PlayerFetchStateId::AWAIT_BAND)
    , playerProvider(playerProvider)
    , bandRegistration(bandRegistration)
    , firmwareUpdateManager(firmwareUpdateManager) {
    setOnJackState(
    [this](SerialIdentifier jack, const JackConnectionState& state) { onJackState(jack, state); });
}

AwaitBandState::~AwaitBandState() {
    playerProvider = nullptr;
    bandRegistration = nullptr;
    firmwareUpdateManager = nullptr;
}

void AwaitBandState::onStateMounted(PDN* pdn) {
    // Only a key and profile taken while this screen's attempt is open may register the PDN.
    playerProvider->clear();
    bandRegistration->open();
    // A hold seen in a later loop yields to the attempt; see onStateLoop.
    bandRegistration->setOnAnswerChanged([this]() { transitionToFetchUserState = true; });
    sinceMount.setTimer(0);
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(true);
    }
    displayIsDirty = true;
}

void AwaitBandState::onStateLoop(PDN* pdn) {
    if (!transitionToFetchUserState) {
        if (isHoldComplete(pdn->getSecondaryButton()->longPressedMillis(),
                           PLAYER_FETCH_FIRMWARE_HOLD_MS)) {
            transitionToFirmwareUpdateState = true;
        } else if (isHoldComplete(pdn->getPrimaryButton()->longPressedMillis(),
                                  PLAYER_FETCH_DEMO_HOLD_MS)) {
            transitionToChooseTeamState = true;
        }
    }
    if (displayIsDirty) {
        renderWaitingScreen(pdn);
        displayIsDirty = false;
    }
}

void AwaitBandState::onStateDismounted(PDN* pdn) {
    bandRegistration->setOnAnswerChanged(nullptr);
    // FetchUser carries the attempt on; any other exit ends it.
    if (!transitionToFetchUserState) {
        bandRegistration->close(false);
    }
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    turnIsOpenByJack.fill(false);
    displayIsDirty = false;
    transitionToFetchUserState = false;
    transitionToChooseTeamState = false;
    transitionToFirmwareUpdateState = false;
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(false);
    }
}

bool AwaitBandState::transitionToFetchUser() const { return transitionToFetchUserState; }

bool AwaitBandState::transitionToChooseTeam() const { return transitionToChooseTeamState; }

bool AwaitBandState::transitionToFirmwareUpdate() const { return transitionToFirmwareUpdateState; }

bool AwaitBandState::isPrimaryRequired() { return false; }

bool AwaitBandState::isAuxRequired() { return false; }

void AwaitBandState::onJackState(SerialIdentifier jack, const JackConnectionState& state) {
    FdnProfile fdn{};
    const bool fromFdn = state.connected && state.context.has_value() &&
                         state.context->peerType == DeviceType::FDN &&
                         state.context->profileLen >= sizeof(fdn);
    if (fromFdn) memcpy(&fdn, state.context->profile.data(), sizeof(fdn));
    const bool turnIsOpen = fromFdn && fdn.registrationTurn == RegistrationTurn::OPEN;
    bool& held = turnIsOpenByJack[static_cast<size_t>(jack)];
    if (held != turnIsOpen) {
        held = turnIsOpen;
        displayIsDirty = true;
    }
}

bool AwaitBandState::isHoldComplete(unsigned long heldMs, unsigned long holdMs) {
    // The pressed time counts from press-down, so a hold carried in from the last
    // screen is longer than this one has been up and does not count.
    return heldMs >= holdMs && heldMs <= sinceMount.getElapsedTime();
}

void AwaitBandState::renderWaitingScreen(PDN* pdn) const {
    const bool turnIsOpen =
    std::find(turnIsOpenByJack.begin(), turnIsOpenByJack.end(), true) != turnIsOpenByJack.end();
    pdn->getDisplay()->invalidateScreen()->setGlyphMode(FontMode::TEXT);
    if (turnIsOpen) {
        pdn->getDisplay()->drawText("Tap band", 16, 26)->drawText("at FDN", 28, 42);
    } else {
        pdn->getDisplay()->drawText("Please", 28, 26)->drawText("wait", 40, 42);
    }
    pdn->getDisplay()->render();
}
