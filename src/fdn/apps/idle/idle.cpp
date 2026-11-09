#include "apps/idle/idle.hpp"
#include "apps/idle/idle-states.hpp"
#include "apps/fdn-app-ids.hpp"
#include <functional>

IdleApp::IdleApp(RemotePlayerManager* remotePlayerManager,
                 RemoteDeviceCoordinator* remoteDeviceCoordinator, FdnMatchSpool* fdnMatchSpool,
                 FdnProvisioningManager* fdnProvisioningManager)
    : StateMachine(IDLE_APP_ID)
    , remotePlayerManager(remotePlayerManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , fdnMatchSpool(fdnMatchSpool)
    , fdnProvisioningManager(fdnProvisioningManager) {}

void IdleApp::populateStateMap() {
    auto* idleState = new IdleState(remotePlayerManager, remoteDeviceCoordinator, fdnMatchSpool,
                                    fdnProvisioningManager, &uploadTimer);
    auto* playerDetectedState =
    new PlayerDetectedState(remotePlayerManager, remoteDeviceCoordinator);
    auto* uploadPendingState = new UploadPendingState(fdnMatchSpool, &uploadTimer);
    auto* registrationState =
    new RegistrationState(fdnProvisioningManager, remoteDeviceCoordinator);

    FdnProvisioningManager* provisioning = fdnProvisioningManager;
    auto toRegistration = [provisioning](FDNConnectState* state) {
        return state->isConnected() && provisioning->hasUnprovisionedPdnCabled();
    };
    auto toSymbolMatch = [provisioning](FDNConnectState* state) {
        return state->isConnected() && provisioning->hasRegisteredPdnCabled() &&
               !provisioning->hasUnprovisionedPdnCabled();
    };

    idleState->addTransition(new StateTransition(
    [idleState, toRegistration]() { return toRegistration(idleState); }, registrationState));
    idleState->addAppTransition([idleState, toSymbolMatch]() { return toSymbolMatch(idleState); },
                                StateId(SYMBOL_MATCH_APP_ID));
    idleState->addTransition(new StateTransition(
    std::bind(&IdleState::transitionToPlayerDetected, idleState), playerDetectedState));
    idleState->addTransition(new StateTransition(
        std::bind(&IdleState::transitionToUploadPending, idleState), uploadPendingState));

    playerDetectedState->addTransition(new StateTransition(
    [playerDetectedState, toRegistration]() { return toRegistration(playerDetectedState); },
    registrationState));
    playerDetectedState->addAppTransition(
    [playerDetectedState, toSymbolMatch]() { return toSymbolMatch(playerDetectedState); },
    StateId(SYMBOL_MATCH_APP_ID));
    playerDetectedState->addTransition(new StateTransition(
    std::bind(&PlayerDetectedState::transitionToIdle, playerDetectedState), idleState));

    uploadPendingState->addTransition(new StateTransition(
    std::bind(&UploadPendingState::transitionToIdle, uploadPendingState), idleState));

    registrationState->addTransition(new StateTransition(
    std::bind(&RegistrationState::transitionToIdle, registrationState), idleState));

    stateMap.push_back(idleState);            // [0] IDLE
    stateMap.push_back(playerDetectedState);  // [1] PLAYER_DETECTED
    stateMap.push_back(uploadPendingState);   // [2] UPLOAD_PENDING
    stateMap.push_back(registrationState);    // [3] REGISTRATION
}
