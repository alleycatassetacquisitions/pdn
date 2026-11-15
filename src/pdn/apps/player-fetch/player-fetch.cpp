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
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider, testIdentity);

    // Nothing checks a top-level app's own transition list, so registration's
    // hand-off to gameplay lives on the state that triggers it.
    chooseTeam->addAppTransition([chooseTeam]() { return chooseTeam->transitionToHub(); },
                                 StateId(HUB_APP_ID));

    stateMap.push_back(chooseTeam);

    if (firmwareUpdateManager != nullptr) {
        FirmwareUpdate* firmwareUpdate =
        new FirmwareUpdate(PlayerFetchStateId::FIRMWARE_UPDATE, firmwareUpdateManager);
        chooseTeam->addTransition(new StateTransition(
        [chooseTeam]() { return chooseTeam->transitionToFirmwareUpdate(); }, firmwareUpdate));
        firmwareUpdate->addTransition(new StateTransition(
        [firmwareUpdate]() { return firmwareUpdate->transitionToHost(); }, chooseTeam));
        stateMap.push_back(firmwareUpdate);
    }
}
