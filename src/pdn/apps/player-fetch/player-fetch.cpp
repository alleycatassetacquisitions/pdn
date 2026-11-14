#include "apps/player-fetch/player-fetch.hpp"
#include "device/firmware-update-state.hpp"

PlayerFetchApp::PlayerFetchApp(WirelessManager* wirelessManager,
                               FirmwareUpdateManager* firmwareUpdateManager,
                               PlayerProvider* playerProvider)
    : wirelessManager(wirelessManager)
    , firmwareUpdateManager(firmwareUpdateManager)
    , playerProvider(playerProvider) {}

void PlayerFetchApp::populateStateMap() {
    AwaitBandState* awaitBand = new AwaitBandState(wirelessManager, playerProvider);
    ChooseTeamState* chooseTeam = new ChooseTeamState(playerProvider);
    PlayerFetchWelcome* welcome = new PlayerFetchWelcome(playerProvider);

    awaitBand->addTransition(
    new StateTransition([awaitBand]() { return awaitBand->transitionToWelcome(); }, welcome));
    awaitBand->addTransition(
    new StateTransition([awaitBand]() { return awaitBand->transitionToChooseTeam(); }, chooseTeam));

    // Nothing checks a top-level app's own transition list, so registration's
    // hand-off to gameplay lives on the state that triggers it.
    chooseTeam->addAppTransition([chooseTeam]() { return chooseTeam->transitionToHub(); },
                                 StateId(HUB_APP_ID));
    welcome->addAppTransition(
        [welcome]() { return welcome->transitionToAwaken(); },
        StateId(HUB_APP_ID));

    stateMap.push_back(awaitBand);
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
