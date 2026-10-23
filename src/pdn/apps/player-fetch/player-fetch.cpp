#include "apps/player-fetch/player-fetch.hpp"
#include "device/firmware-update-state.hpp"

PlayerFetchApp::PlayerFetchApp(WirelessManager* wirelessManager,
                               FirmwareUpdateManager* firmwareUpdateManager,
                               PlayerProvider* playerProvider)
    : StateMachine(PLAYER_FETCH_APP_ID)
    , wirelessManager(wirelessManager)
    , firmwareUpdateManager(firmwareUpdateManager)
    , playerProvider(playerProvider) {}

PlayerFetchApp::~PlayerFetchApp() {
    wirelessManager = nullptr;
    firmwareUpdateManager = nullptr;
    playerProvider = nullptr;
}

void PlayerFetchApp::populateStateMap() {
    PinEntryState* pinEntry = new PinEntryState(playerProvider);
    FetchUserState* fetchUser = new FetchUserState(wirelessManager, playerProvider);
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider);
    DemoStubState* demoStub = new DemoStubState();
    PlayerFetchWelcome* welcome = new PlayerFetchWelcome(playerProvider);
    PlayerFetchAwakenSequence* awaken = new PlayerFetchAwakenSequence();

    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToFetchUser(); }, fetchUser));
    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToChooseTeam(); }, chooseTeam));

    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToWelcome(); }, welcome));
    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToPinEntry(); }, pinEntry));

    chooseTeam->addTransition(
    new StateTransition([chooseTeam]() { return chooseTeam->transitionToDemoStub(); }, demoStub));

    demoStub->addTransition(
    new StateTransition([demoStub]() { return demoStub->transitionToPinEntry(); }, pinEntry));

    welcome->addTransition(
    new StateTransition([welcome]() { return welcome->transitionToAwaken(); }, awaken));

    stateMap.push_back(pinEntry);
    stateMap.push_back(fetchUser);
    stateMap.push_back(chooseTeam);
    stateMap.push_back(demoStub);
    stateMap.push_back(welcome);
    stateMap.push_back(awaken);

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

bool PlayerFetchApp::readyForGameplay() {
    if (currentState->getStateId() != PlayerFetchStateId::AWAKEN) {
        return false;
    }
    PlayerFetchAwakenSequence* awaken = static_cast<PlayerFetchAwakenSequence*>(currentState);
    return awaken->transitionToComplete();
}
