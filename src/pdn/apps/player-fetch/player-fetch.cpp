#include "apps/player-fetch/player-fetch.hpp"
#include "device/firmware-update-state.hpp"

PlayerFetchApp::PlayerFetchApp(WirelessManager* wirelessManager,
                               FirmwareUpdateManager* firmwareUpdateManager,
                               PlayerProvider* playerProvider)
    : StateMachine(PLAYER_FETCH_APP_ID)
    , provisioningClient(wirelessManager)
    , firmwareUpdateManager(firmwareUpdateManager)
    , playerProvider(playerProvider) {}

void PlayerFetchApp::populateStateMap() {
    PinEntryState* pinEntry = new PinEntryState(playerProvider);
    FetchUserState* fetchUser = new FetchUserState(&provisioningClient, playerProvider);
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider);
    PlayerFetchWelcome* welcome = new PlayerFetchWelcome(playerProvider);

    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToFetchUser(); }, fetchUser));
    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToChooseTeam(); }, chooseTeam));

    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToWelcome(); }, welcome));
    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToPinEntry(); }, pinEntry));

    // Nothing checks a top-level app's own transition list, so registration's
    // hand-off to gameplay lives on the state that triggers it.
    chooseTeam->addAppTransition([chooseTeam]() { return chooseTeam->transitionToHub(); },
                                 StateId(HUB_APP_ID));
    welcome->addAppTransition(
        [welcome]() { return welcome->transitionToAwaken(); },
        StateId(HUB_APP_ID));

    stateMap.push_back(pinEntry);
    stateMap.push_back(fetchUser);
    stateMap.push_back(chooseTeam);
    stateMap.push_back(welcome);

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
