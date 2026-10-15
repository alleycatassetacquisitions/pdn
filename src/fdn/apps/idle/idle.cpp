#include "apps/idle/idle.hpp"
#include "apps/idle/idle-states.hpp"
#include "apps/fdn-app-ids.hpp"

Idle::Idle(RemotePlayerManager* remotePlayerManager,
           HackedPlayersManager* hackedPlayersManager,
           FDNConnectWirelessManager* fdnConnectWirelessManager,
           RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : StateMachine(IDLE_APP_ID)
    , remotePlayerManager(remotePlayerManager)
    , hackedPlayersManager(hackedPlayersManager)
    , fdnConnectWirelessManager(fdnConnectWirelessManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

Idle::~Idle() {
    remotePlayerManager       = nullptr;
    hackedPlayersManager      = nullptr;
    fdnConnectWirelessManager = nullptr;
    remoteDeviceCoordinator   = nullptr;
}

void Idle::populateStateMap() {
    auto* idleState = new IdleState(
        remotePlayerManager, hackedPlayersManager,
        fdnConnectWirelessManager, remoteDeviceCoordinator);
    auto* playerDetectedState = new PlayerDetectedState(
        remotePlayerManager, hackedPlayersManager,
        fdnConnectWirelessManager, remoteDeviceCoordinator);
    auto* authDetectedState         = new AuthDetectedState(remoteDeviceCoordinator, fdnConnectWirelessManager);
    auto* unauthorizedDetectedState = new UnauthorizedDetectedState(remoteDeviceCoordinator);
    auto* connectionDetectedState   = new ConnectionDetectedState(
        hackedPlayersManager, fdnConnectWirelessManager, remoteDeviceCoordinator);
    auto* uploadPendingState = new UploadPendingHacksState(hackedPlayersManager);

    // Shared connect handler: forwards the player ID to ConnectionDetectedState.
    auto connectHandler = [connectionDetectedState](
        const std::string& playerId, const uint8_t*) {
        connectionDetectedState->receivePdnConnection(playerId);
    };
    idleState->setConnectionHandler(connectHandler);
    playerDetectedState->setConnectionHandler(connectHandler);

    idleState->addTransition(new StateTransition(
        std::bind(&IdleState::transitionToPlayerDetected, idleState), playerDetectedState));
    idleState->addTransition(new StateTransition(
        std::bind(&IdleState::transitionToConnectionDetected, idleState), connectionDetectedState));
    idleState->addTransition(new StateTransition(
        std::bind(&IdleState::transitionToUploadPending, idleState), uploadPendingState));

    playerDetectedState->addTransition(new StateTransition(
        std::bind(&PlayerDetectedState::transitionToIdle, playerDetectedState), idleState));
    playerDetectedState->addTransition(new StateTransition(
        std::bind(&PlayerDetectedState::transitionToConnectionDetected, playerDetectedState),
        connectionDetectedState));

    connectionDetectedState->addTransition(new StateTransition(
        std::bind(&ConnectionDetectedState::transitionToIdle, connectionDetectedState), idleState));
    connectionDetectedState->addTransition(new StateTransition(
        std::bind(&ConnectionDetectedState::transitionToAuth, connectionDetectedState),
        authDetectedState));
    connectionDetectedState->addAppTransition(
        std::bind(&ConnectionDetectedState::transitionToUnauthorized, connectionDetectedState),
        StateId(SYMBOL_MATCH_APP_ID));

    authDetectedState->addTransition(new StateTransition(
        std::bind(&AuthDetectedState::transitionToIdle, authDetectedState), idleState));
    authDetectedState->addAppTransition(
        std::bind(&AuthDetectedState::transitionToMainMenu, authDetectedState),
        StateId(MAIN_MENU_APP_ID));

    unauthorizedDetectedState->addTransition(new StateTransition(
        std::bind(&UnauthorizedDetectedState::transitionToIdle, unauthorizedDetectedState), idleState));
    unauthorizedDetectedState->addAppTransition(
        std::bind(&UnauthorizedDetectedState::transitionToHacking, unauthorizedDetectedState),
        StateId(HACKING_APP_ID));

    uploadPendingState->addTransition(new StateTransition(
        std::bind(&UploadPendingHacksState::transitionToIdle, uploadPendingState), idleState));

    stateMap.push_back(idleState);                 // [0] IDLE
    stateMap.push_back(playerDetectedState);        // [1] PLAYER_DETECTED
    stateMap.push_back(authDetectedState);          // [2] AUTHORIZED_PDN
    stateMap.push_back(unauthorizedDetectedState);  // [3] UNAUTHORIZED_PDN
    stateMap.push_back(connectionDetectedState);    // [4] CONNECTION_DETECTED
    stateMap.push_back(uploadPendingState);         // [5] UPLOAD_PENDING
}
