#include "apps/idle/idle.hpp"
#include "apps/idle/idle-states.hpp"
#include "apps/fdn-app-ids.hpp"
#include <functional>

IdleApp::IdleApp(RemotePlayerManager* remotePlayerManager,
                 RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : StateMachine(IDLE_APP_ID)
    , remotePlayerManager(remotePlayerManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

void IdleApp::populateStateMap() {
    auto* idleState = new IdleState(remotePlayerManager, remoteDeviceCoordinator);
    auto* playerDetectedState =
    new PlayerDetectedState(remotePlayerManager, remoteDeviceCoordinator);

    RemoteDeviceCoordinator* rdc = remoteDeviceCoordinator;
    auto toSymbolMatch = [rdc]() { return rdc->getConnectedPeerMac(DeviceType::PDN) != nullptr; };

    idleState->addAppTransition(toSymbolMatch, StateId(SYMBOL_MATCH_APP_ID));
    idleState->addTransition(new StateTransition(
    std::bind(&IdleState::transitionToPlayerDetected, idleState), playerDetectedState));

    playerDetectedState->addAppTransition(toSymbolMatch, StateId(SYMBOL_MATCH_APP_ID));
    playerDetectedState->addTransition(new StateTransition(
    std::bind(&PlayerDetectedState::transitionToIdle, playerDetectedState), idleState));

    stateMap.push_back(idleState);            // [0] IDLE
    stateMap.push_back(playerDetectedState);  // [1] PLAYER_DETECTED
}
