#include "apps/controller/controller.hpp"
#include "apps/controller/controller-states.hpp"

Controller::Controller(Player* player,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator,
                       SymbolWirelessManager* symbolWirelessManager,
                       ControllerWirelessManager* controllerWirelessManager)
    : TypedStateMachine<PDN>(CONTROLLER_APP_ID)
    , player(player)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , symbolWirelessManager(symbolWirelessManager)
    , controllerWirelessManager(controllerWirelessManager)
    , disconnectPolicy(remoteDeviceCoordinator) {}

Controller::~Controller() {}

void Controller::onStateMounted(Device* device) {
    if (!hasLaunched()) {
        TypedStateMachine<PDN>::onStateMounted(device);
        return;
    }
    // On re-entry (e.g. after a brief Quickdraw detour), resume directly at
    // Controller1 — symbol matching is already done and should not restart.
    skipToState(device, kController1StateIndex);
}

void Controller::populateStateMap() {
    auto* symbolState = new SymbolState(player, remoteDeviceCoordinator, symbolWirelessManager);
    auto* symbolMatchedState = new SymbolMatchedState(
        player, remoteDeviceCoordinator, symbolWirelessManager, controllerWirelessManager);
    auto* controller1State = new Controller1State(controllerWirelessManager);

    symbolState->addTransition(new StateTransition(
        std::bind(&SymbolState::transitionToSymbolMatched, symbolState),
        symbolMatchedState));

    symbolMatchedState->addTransition(new StateTransition(
        std::bind(&SymbolMatchedState::transitionToSymbol, symbolMatchedState),
        symbolState));

    symbolMatchedState->addTransition(new StateTransition(
        std::bind(&SymbolMatchedState::transitionToController1, symbolMatchedState),
        controller1State));

    stateMap.push_back(symbolState);
    stateMap.push_back(symbolMatchedState);
    stateMap.push_back(controller1State);

    for (State* state : stateMap) {
        state->addAppTransition(
            [this]() { return disconnectPolicy.isPersistentlyDisconnected(); },
            StateId(QUICKDRAW_APP_ID));
    }
}