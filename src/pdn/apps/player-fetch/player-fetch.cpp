#include "apps/player-fetch/player-fetch.hpp"
#include "apps/device-test/device-test-app.hpp"
#include "apps/device-test/device-test-subject-state.hpp"
#include "device/firmware-update-state.hpp"

PlayerFetchApp::PlayerFetchApp(WirelessManager* wirelessManager,
                               FirmwareUpdateManager* firmwareUpdateManager,
                               PlayerProvider* playerProvider,
                               RemoteDeviceCoordinator* remoteDeviceCoordinator,
                               DeviceTestWirelessManager* deviceTestWirelessManager)
    : StateMachine(PLAYER_FETCH_APP_ID)
    , wirelessManager(wirelessManager)
    , firmwareUpdateManager(firmwareUpdateManager)
    , playerProvider(playerProvider)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , deviceTestWirelessManager(deviceTestWirelessManager) {}

PlayerFetchApp::~PlayerFetchApp() {
    wirelessManager = nullptr;
    firmwareUpdateManager = nullptr;
    playerProvider = nullptr;
    remoteDeviceCoordinator = nullptr;
    deviceTestWirelessManager = nullptr;
}

void PlayerFetchApp::populateStateMap() {
    PinEntryState* pinEntry = new PinEntryState(playerProvider, deviceTestWirelessManager);
    FetchUserState* fetchUser = new FetchUserState(wirelessManager, playerProvider);
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider);
    DemoStubState* demoStub = new DemoStubState();
    PlayerFetchWelcome* welcome = new PlayerFetchWelcome(playerProvider);
    DeviceTestApp* deviceTestApp = new DeviceTestApp(remoteDeviceCoordinator,
                                                    deviceTestWirelessManager,
                                                    wirelessManager,
                                                    PlayerFetchStateId::DEVICE_TEST);
    DeviceTestSubjectState* deviceTestSubject =
        new DeviceTestSubjectState(remoteDeviceCoordinator, deviceTestWirelessManager);

    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToFetchUser(); }, fetchUser));
    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToChooseTeam(); }, chooseTeam));
    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToDeviceTest(); }, deviceTestApp));
    pinEntry->addTransition(
    new StateTransition([pinEntry]() { return pinEntry->transitionToDeviceTestSubject(); }, deviceTestSubject));

    deviceTestSubject->addTransition(
        new StateTransition([deviceTestSubject]() { return deviceTestSubject->transitionToPinEntry(); }, pinEntry));

    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToWelcome(); }, welcome));
    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToPinEntry(); }, pinEntry));

    chooseTeam->addTransition(
    new StateTransition([chooseTeam]() { return chooseTeam->transitionToDemoStub(); }, demoStub));

    demoStub->addTransition(
    new StateTransition([demoStub]() { return demoStub->transitionToPinEntry(); }, pinEntry));

    stateMap.push_back(pinEntry);
    stateMap.push_back(fetchUser);
    stateMap.push_back(chooseTeam);
    stateMap.push_back(demoStub);
    stateMap.push_back(welcome);
    stateMap.push_back(deviceTestApp);
    stateMap.push_back(deviceTestSubject);

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
    if (currentState->getStateId() != PlayerFetchStateId::WELCOME) {
        return false;
    }
    PlayerFetchWelcome* welcome = static_cast<PlayerFetchWelcome*>(currentState);
    return welcome->transitionToAwaken();
}
