#include "apps/player-registration/player-registration.hpp"
#include "device/firmware-update-state.hpp"

PlayerRegistrationApp::PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager,
                                             RemoteDebugManager* remoteDebugManager,
                                             FirmwareUpdateManager* firmwareUpdateManager)
    : StateMachine(PLAYER_REGISTRATION_APP_ID)
    , firmwareUpdateManager(firmwareUpdateManager) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->matchManager = matchManager;
    this->remoteDebugManager = remoteDebugManager;
}

PlayerRegistrationApp::~PlayerRegistrationApp() {
    player = nullptr;
    wirelessManager = nullptr;
    matchManager = nullptr;
    remoteDebugManager = nullptr;
}

void PlayerRegistrationApp::populateStateMap() {
    PlayerRegistrationState* playerRegistration = new PlayerRegistrationState(player, matchManager);
    FetchUserDataState* fetchUserDataState = new FetchUserDataState(player, wirelessManager, remoteDebugManager, matchManager);
    ConfirmOfflineState* confirmOffline = new ConfirmOfflineState(player);
    ChooseRoleState* chooseRole = new ChooseRoleState(player);
    WelcomeMessage* welcomeMessageState = new WelcomeMessage(player);

    playerRegistration->addTransition(
        new StateTransition(
            std::bind(&PlayerRegistrationState::transitionToUserFetch, playerRegistration),
            fetchUserDataState));

    fetchUserDataState->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToConfirmOffline, fetchUserDataState),
            confirmOffline));

    fetchUserDataState->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToWelcomeMessage, fetchUserDataState),
            welcomeMessageState));

    fetchUserDataState->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToPlayerRegistration, fetchUserDataState),
            playerRegistration));

    confirmOffline->addTransition(
        new StateTransition(
            std::bind(&ConfirmOfflineState::transitionToChooseRole, confirmOffline),
            chooseRole));

    confirmOffline->addTransition(
        new StateTransition(
            std::bind(&ConfirmOfflineState::transitionToPlayerRegistration, confirmOffline),
            playerRegistration));

    chooseRole->addTransition(
        new StateTransition(
            std::bind(&ChooseRoleState::transitionToWelcomeMessage, chooseRole),
            welcomeMessageState));

    stateMap.push_back(playerRegistration);
    stateMap.push_back(fetchUserDataState);
    stateMap.push_back(confirmOffline);
    stateMap.push_back(chooseRole);
    stateMap.push_back(welcomeMessageState);

    // Hosted here, not in Quickdraw's map: re-mounting this app re-runs populateStateMap,
    // which appends a duplicate set of states.
    if (firmwareUpdateManager != nullptr) {
        FirmwareUpdate* firmwareUpdate = new FirmwareUpdate(REGISTRATION_FIRMWARE_UPDATE, firmwareUpdateManager);
        playerRegistration->addTransition(new StateTransition(
            [playerRegistration]() { return playerRegistration->transitionToFirmwareUpdate(); }, firmwareUpdate));
        firmwareUpdate->addTransition(
            new StateTransition([firmwareUpdate]() { return firmwareUpdate->transitionToHost(); }, playerRegistration));
        stateMap.push_back(firmwareUpdate);
    }
}

bool PlayerRegistrationApp::readyForGameplay() {
    return (currentState->getStateId() == PlayerRegistrationStateId::WELCOME_MESSAGE && static_cast<WelcomeMessage*>(currentState)->transitionToGameplay());
}