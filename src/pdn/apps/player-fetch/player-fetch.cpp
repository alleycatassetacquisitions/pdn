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
    FetchUserState* fetchUser = new FetchUserState(wirelessManager, playerProvider);
    PlayerFetchWelcome* welcome = new PlayerFetchWelcome(playerProvider);

    fetchUser->addTransition(
    new StateTransition([fetchUser]() { return fetchUser->transitionToWelcome(); }, welcome));

    // Nothing checks a top-level app's own transition list, so registration's
    // hand-off to gameplay lives on the state that triggers it.
    welcome->addAppTransition(
        [welcome]() { return welcome->transitionToAwaken(); },
        StateId(HUB_APP_ID));

    stateMap.push_back(fetchUser);
    stateMap.push_back(welcome);

    if (firmwareUpdateManager != nullptr) {
        FirmwareUpdate* firmwareUpdate =
        new FirmwareUpdate(PlayerFetchStateId::FIRMWARE_UPDATE, firmwareUpdateManager);
        fetchUser->addTransition(new StateTransition(
        [fetchUser]() { return fetchUser->transitionToFirmwareUpdate(); }, firmwareUpdate));
        firmwareUpdate->addTransition(new StateTransition(
        [firmwareUpdate]() { return firmwareUpdate->transitionToHost(); }, fetchUser));
        stateMap.push_back(firmwareUpdate);
    }
}
