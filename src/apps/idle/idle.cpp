#include <functional>
#include "apps/idle/idle.hpp"
#include "apps/idle/states/idle-state.hpp"
#include "apps/idle/states/player-detected-state.hpp"
#include "apps/idle/states/connection-detected-state.hpp"
#include "apps/idle/states/auth-detected-state.hpp"
#include "apps/idle/states/unauthorized-detected-state.hpp"
#include "apps/idle/states/upload-pending-hacks-state.hpp"

Idle::Idle(RemotePlayerManager* remotePlayerManager,
           HackedPlayersManager* hackedPlayersManager,
           FDNConnectWirelessManager* fdnConnectWirelessManager,
           RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : StateMachine(IDLE_APP_ID) {
    this->remotePlayerManager = remotePlayerManager;
    this->hackedPlayersManager = hackedPlayersManager;
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
    this->remoteDeviceCoordinator = remoteDeviceCoordinator;
}

Idle::~Idle() {
    remotePlayerManager = nullptr;
    hackedPlayersManager = nullptr;
    fdnConnectWirelessManager = nullptr;
    remoteDeviceCoordinator = nullptr;
}

void Idle::populateStateMap() {
    auto* idleState = new IdleState(
        remotePlayerManager, hackedPlayersManager, fdnConnectWirelessManager, remoteDeviceCoordinator);
    auto* playerDetectedState = new PlayerDetectedState(
        remotePlayerManager, hackedPlayersManager, fdnConnectWirelessManager, remoteDeviceCoordinator);
    auto* authDetectedState        = new AuthDetectedState(remoteDeviceCoordinator);
    auto* unauthorizedDetectedState = new UnauthorizedDetectedState(remoteDeviceCoordinator);
    auto* connectionDetectedState  = new ConnectionDetectedState(
        hackedPlayersManager, fdnConnectWirelessManager, remoteDeviceCoordinator);
    auto* uploadPendingState = new UploadPendingHacksState(hackedPlayersManager);

    // Shared connect handler: forwards the player ID to ConnectionDetectedState
    // and is registered by both IdleState and PlayerDetectedState on mount.
    auto connectHandler = [connectionDetectedState](
        const std::string& playerId, const uint8_t* /*senderMac*/) {
        connectionDetectedState->receivePdnConnection(playerId);
    };
    idleState->setConnectionHandler(connectHandler);
    playerDetectedState->setConnectionHandler(connectHandler);

    // IdleState transitions
    idleState->addTransition(new StateTransition(
        std::bind(&IdleState::transitionToPlayerDetected, idleState), playerDetectedState));
    idleState->addTransition(new StateTransition(
        std::bind(&IdleState::transitionToConnectionDetected, idleState), connectionDetectedState));
    idleState->addTransition(new StateTransition(
        std::bind(&IdleState::transitionToUploadPending, idleState), uploadPendingState));

    // PlayerDetectedState transitions
    playerDetectedState->addTransition(new StateTransition(
        std::bind(&PlayerDetectedState::transitionToIdle, playerDetectedState), idleState));
    playerDetectedState->addTransition(new StateTransition(
        std::bind(&PlayerDetectedState::transitionToConnectionDetected, playerDetectedState), connectionDetectedState));

    // ConnectionDetectedState transitions
    connectionDetectedState->addTransition(new StateTransition(
        std::bind(&ConnectionDetectedState::transitionToAuth, connectionDetectedState), authDetectedState));
    connectionDetectedState->addTransition(new StateTransition(
        std::bind(&ConnectionDetectedState::transitionToUnauthorized, connectionDetectedState), unauthorizedDetectedState));

    // UploadPendingHacksState transitions
    uploadPendingState->addTransition(new StateTransition(
        std::bind(&UploadPendingHacksState::transitionToIdle, uploadPendingState), idleState));

    // AuthDetectedState and UnauthorizedDetectedState are terminal within this app —
    // they switch apps directly and have no outgoing StateTransitions.

    stateMap.push_back(idleState);                 // [0] IDLE
    stateMap.push_back(playerDetectedState);        // [1] PLAYER_DETECTED
    stateMap.push_back(authDetectedState);          // [2] AUTHORIZED_PDN
    stateMap.push_back(unauthorizedDetectedState);  // [3] UNAUTHORIZED_PDN
    stateMap.push_back(connectionDetectedState);    // [4] CONNECTION_DETECTED
    stateMap.push_back(uploadPendingState);         // [5] UPLOAD_PENDING
}
