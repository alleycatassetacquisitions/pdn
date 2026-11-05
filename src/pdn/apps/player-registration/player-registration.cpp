#include "apps/player-registration/player-registration.hpp"

PlayerRegistrationApp::PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager)
    : StateMachine(PLAYER_REGISTRATION_APP_ID) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->remoteDebugManager = remoteDebugManager;
}

PlayerRegistrationApp::~PlayerRegistrationApp() {
    player = nullptr;
    wirelessManager = nullptr;
    remoteDebugManager = nullptr;
}

void PlayerRegistrationApp::populateStateMap() {
    PlayerRegistrationState* playerRegistration = new PlayerRegistrationState(player);
    FetchUserDataState* fetchUserDataState = new FetchUserDataState(player, wirelessManager, remoteDebugManager);
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

    // Nothing checks a top-level app's own transition list, so registration's
    // hand-off to gameplay lives on the state that triggers it.
    welcomeMessageState->addAppTransition(
        [welcomeMessageState]() { return welcomeMessageState->transitionToGameplay(); },
        StateId(HUB_APP_ID));

    stateMap.push_back(playerRegistration);
    stateMap.push_back(fetchUserDataState);
    stateMap.push_back(confirmOffline);
    stateMap.push_back(chooseRole);
    stateMap.push_back(welcomeMessageState);
}