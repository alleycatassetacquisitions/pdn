#include "apps/player-registration/player-registration.hpp"

PlayerRegistrationApp::PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager, RemoteDebugManager* remoteDebugManager)
    : StateMachine(PLAYER_REGISTRATION_APP_ID) {
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
    ChooseRoleState* chooseRole = new ChooseRoleState(player);
    WelcomeMessage* welcomeMessageState = new WelcomeMessage(player);

    confirmOfflinePages[0] = { {"Unable to", "Locate Asset", ""}, 2 };
    confirmOfflinePages[1] = { {"Proceed with", "Pairing Code", ""}, 3 };
    confirmOfflineConfig   = { confirmOfflinePages, 2, "Confirm", "Reset" };

    InstructionsState* confirmOffline = new InstructionsState(CONFIRM_OFFLINE, confirmOfflineConfig);
    confirmOffline->onBeforeMount = [this]() {
        confirmOfflinePages[1].lines[2] = player->getUserID();
    };

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
            std::bind(&InstructionsState::optionOneSelectedEvent, confirmOffline),
            chooseRole));

    confirmOffline->addTransition(
        new StateTransition(
            std::bind(&InstructionsState::optionTwoSelectedEvent, confirmOffline),
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
}

bool PlayerRegistrationApp::readyForGameplay() {
    return (currentState->getStateId() == PlayerRegistrationStateId::WELCOME_MESSAGE && static_cast<WelcomeMessage*>(currentState)->transitionToGameplay());
}