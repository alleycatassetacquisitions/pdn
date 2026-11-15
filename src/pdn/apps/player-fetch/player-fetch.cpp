#include "apps/player-fetch/player-fetch.hpp"
#include "device/firmware-update-state.hpp"

PlayerFetchApp::PlayerFetchApp(WirelessManager* wirelessManager,
                               FirmwareUpdateManager* firmwareUpdateManager,
                               PlayerProvider* playerProvider, bool testIdentity)
    : StateMachine(PLAYER_FETCH_APP_ID)
    , wirelessManager(wirelessManager)
    , firmwareUpdateManager(firmwareUpdateManager)
    , playerProvider(playerProvider)
    , testIdentity(testIdentity) {}

PlayerFetchApp::~PlayerFetchApp() {
    wirelessManager = nullptr;
    firmwareUpdateManager = nullptr;
    playerProvider = nullptr;
}

void PlayerFetchApp::populateStateMap() {
    PinEntryState* pinEntry = new PinEntryState(playerProvider);
    FetchUserState* fetchUser = new FetchUserState(wirelessManager, playerProvider);
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider, testIdentity);
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
    welcome->addAppTransition(
        [welcome]() { return welcome->transitionToAwaken(); },
        StateId(HUB_APP_ID));
    chooseTeam->addAppTransition([chooseTeam]() { return chooseTeam->transitionToHub(); },
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
