#include "apps/player-fetch/player-fetch.hpp"
#include "device/firmware-update-state.hpp"

PlayerFetchApp::PlayerFetchApp(FirmwareUpdateManager* firmwareUpdateManager,
                               PlayerProvider* playerProvider, bool testIdentity)
    : StateMachine(PLAYER_FETCH_APP_ID)
    , firmwareUpdateManager(firmwareUpdateManager)
    , playerProvider(playerProvider)
    , testIdentity(testIdentity) {}

PlayerFetchApp::~PlayerFetchApp() {
    firmwareUpdateManager = nullptr;
    playerProvider = nullptr;
}

void PlayerFetchApp::populateStateMap() {
    PinEntryState* pinEntry = new PinEntryState(playerProvider);
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider, testIdentity);

    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToChooseTeam(); }, chooseTeam));

    // Nothing checks a top-level app's own transition list, so registration's
    // hand-off to gameplay lives on the state that triggers it.
    chooseTeam->addAppTransition([chooseTeam]() { return chooseTeam->transitionToHub(); },
                                 StateId(HUB_APP_ID));

    stateMap.push_back(pinEntry);
    stateMap.push_back(chooseTeam);

    if (firmwareUpdateManager != nullptr) {
        FirmwareUpdate* firmwareUpdate =
        new FirmwareUpdate(PlayerFetchStateId::FIRMWARE_UPDATE, firmwareUpdateManager);
        pinEntry->addTransition(new StateTransition(
        [pinEntry]() { return pinEntry->transitionToFirmwareUpdate(); }, firmwareUpdate));
        firmwareUpdate->addTransition(new StateTransition(
        [firmwareUpdate]() { return firmwareUpdate->transitionToHost(); }, pinEntry));
        stateMap.push_back(firmwareUpdate);
    }
}
