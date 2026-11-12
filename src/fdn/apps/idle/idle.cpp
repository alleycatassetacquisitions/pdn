#include "apps/idle/idle.hpp"
#include "apps/idle/idle-states.hpp"
#include "apps/fdn-app-ids.hpp"
#include <functional>

IdleApp::IdleApp(RemotePlayerManager* remotePlayerManager,
                 RemoteDeviceCoordinator* remoteDeviceCoordinator,
                 FdnProvisioningManager* fdnProvisioningManager)
    : StateMachine(IDLE_APP_ID)
    , remotePlayerManager(remotePlayerManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , fdnProvisioningManager(fdnProvisioningManager) {}

void IdleApp::populateStateMap() {
    auto* idleState = new IdleState(remotePlayerManager, remoteDeviceCoordinator);
    auto* playerDetectedState =
    new PlayerDetectedState(remotePlayerManager, remoteDeviceCoordinator);
    auto* registrationState =
    new RegistrationState(fdnProvisioningManager, remoteDeviceCoordinator);

    FdnProvisioningManager* provisioning = fdnProvisioningManager;
    std::function<bool()> toRegistration = [provisioning]() {
        return provisioning->hasUnprovisionedPdnCabled();
    };
    std::function<bool()> toSymbolMatch = [provisioning]() {
        return provisioning->hasRegisteredPdnCabled() && !provisioning->hasUnprovisionedPdnCabled();
    };

    idleState->addTransition(new StateTransition(toRegistration, registrationState));
    idleState->addAppTransition(toSymbolMatch, StateId(SYMBOL_MATCH_APP_ID));
    idleState->addTransition(new StateTransition(
    std::bind(&IdleState::transitionToPlayerDetected, idleState), playerDetectedState));

    playerDetectedState->addTransition(new StateTransition(toRegistration, registrationState));
    playerDetectedState->addAppTransition(toSymbolMatch, StateId(SYMBOL_MATCH_APP_ID));
    playerDetectedState->addTransition(new StateTransition(
    std::bind(&PlayerDetectedState::transitionToIdle, playerDetectedState), idleState));

    registrationState->addTransition(new StateTransition(
    std::bind(&RegistrationState::transitionToIdle, registrationState), idleState));

    stateMap.push_back(idleState);            // [0] IDLE
    stateMap.push_back(playerDetectedState);  // [1] PLAYER_DETECTED
    stateMap.push_back(registrationState);    // REGISTRATION
}
