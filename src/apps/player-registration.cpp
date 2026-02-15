#include "../../include/apps/player-registration.hpp"
#include "../../include/game/match-manager.hpp"

PlayerRegistrationApp::PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager,
                                             RemoteDebugManager* remoteDebugManager,
                                             ProgressManager* progressManager) :
    StateMachine(PLAYER_REGISTRATION_APP_ID),
    player(player),
    wirelessManager(wirelessManager),
    remoteDebugManager(remoteDebugManager),
    progressManager(progressManager)
{
}

PlayerRegistrationApp::~PlayerRegistrationApp() {
    player = nullptr;
    wirelessManager = nullptr;
    remoteDebugManager = nullptr;
    progressManager = nullptr;
}

void PlayerRegistrationApp::populateStateMap() {
    // Create a minimal MatchManager for PlayerRegistration state
    MatchManager* matchManager = new MatchManager();

    // Instantiate registration flow states
    PlayerRegistration* playerRegistration = new PlayerRegistration(player, matchManager);
    FetchUserDataState* fetchUserData = new FetchUserDataState(player, wirelessManager, remoteDebugManager, progressManager);
    ConfirmOfflineState* confirmOffline = new ConfirmOfflineState(player);
    ChooseRoleState* chooseRole = new ChooseRoleState(player);
    WelcomeMessage* welcomeMessage = new WelcomeMessage(player);

    // Wire transitions
    playerRegistration->addTransition(
        new StateTransition(
            std::bind(&PlayerRegistration::transitionToUserFetch, playerRegistration),
            fetchUserData));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToConfirmOffline, fetchUserData),
            confirmOffline));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToWelcomeMessage, fetchUserData),
            welcomeMessage));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToPlayerRegistration, fetchUserData),
            playerRegistration));

    confirmOffline->addTransition(
        new StateTransition(
            std::bind(&ConfirmOfflineState::transitionToChooseRole, confirmOffline),
            chooseRole));

    confirmOffline->addTransition(
        new StateTransition(
            std::bind(&ConfirmOfflineState::transitionToPlayerRegistration, confirmOffline),
            playerRegistration));

    // Wire ChooseRole directly to WelcomeMessage (skip AllegiancePicker)
    // Using the existing transitionToAllegiancePicker method, but pointing to welcomeMessage
    chooseRole->addTransition(
        new StateTransition(
            std::bind(&ChooseRoleState::transitionToAllegiancePicker, chooseRole),
            welcomeMessage));

    // WelcomeMessage is terminal — set registrationComplete flag
    welcomeMessage->addTransition(
        new StateTransition(
            [this]() {
                this->registrationComplete = true;
                return this->registrationComplete;
            },
            nullptr));

    // Push states to stateMap
    stateMap.push_back(playerRegistration);
    stateMap.push_back(fetchUserData);
    stateMap.push_back(confirmOffline);
    stateMap.push_back(chooseRole);
    stateMap.push_back(welcomeMessage);
}

bool PlayerRegistrationApp::readyForGameplay() {
    return registrationComplete;
}
