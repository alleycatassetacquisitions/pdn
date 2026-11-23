#include "apps/player-fetch/player-fetch.hpp"
#include "device/firmware-update-state.hpp"

PlayerFetchApp::PlayerFetchApp(FirmwareUpdateManager* firmwareUpdateManager,
                               PlayerProvider* playerProvider, BandRegistration* bandRegistration,
                               RemoteDeviceCoordinator* remoteDeviceCoordinator, bool testIdentity)
    : StateMachine(PLAYER_FETCH_APP_ID)
    , firmwareUpdateManager(firmwareUpdateManager)
    , playerProvider(playerProvider)
    , bandRegistration(bandRegistration)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , testIdentity(testIdentity) {}

PlayerFetchApp::~PlayerFetchApp() {
    firmwareUpdateManager = nullptr;
    playerProvider = nullptr;
    bandRegistration = nullptr;
    remoteDeviceCoordinator = nullptr;
}

void PlayerFetchApp::populateStateMap() {
    AwaitBandState* awaitBand = new AwaitBandState(remoteDeviceCoordinator, playerProvider,
                                                   bandRegistration, firmwareUpdateManager);
    FetchUserState* fetchUser = new FetchUserState(playerProvider, bandRegistration);
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider, testIdentity);
    PlayerFetchWelcome* welcome = new PlayerFetchWelcome(playerProvider);

    awaitBand->addTransition(
    new StateTransition([awaitBand]() { return awaitBand->transitionToFetchUser(); }, fetchUser));
    awaitBand->addTransition(
    new StateTransition([awaitBand]() { return awaitBand->transitionToChooseTeam(); }, chooseTeam));
    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToWelcome(); }, welcome));
    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToAwaitBand(); }, awaitBand));
    chooseTeam->addTransition(
    new StateTransition([chooseTeam]() { return chooseTeam->transitionToWelcome(); }, welcome));

    // Nothing checks a top-level app's own transition list, so registration's
    // hand-off to gameplay lives on the state that triggers it.
    welcome->addAppTransition([welcome]() { return welcome->transitionToAwaken(); },
                              StateId(HUB_APP_ID));

    stateMap.push_back(awaitBand);
    stateMap.push_back(fetchUser);
    stateMap.push_back(chooseTeam);
    stateMap.push_back(welcome);

    if (firmwareUpdateManager != nullptr) {
        FirmwareUpdate* firmwareUpdate =
        new FirmwareUpdate(PlayerFetchStateId::FIRMWARE_UPDATE, firmwareUpdateManager);
        awaitBand->addTransition(new StateTransition(
        [awaitBand]() { return awaitBand->transitionToFirmwareUpdate(); }, firmwareUpdate));
        firmwareUpdate->addTransition(new StateTransition(
        [firmwareUpdate]() { return firmwareUpdate->transitionToHost(); }, awaitBand));
        stateMap.push_back(firmwareUpdate);
    }
}
