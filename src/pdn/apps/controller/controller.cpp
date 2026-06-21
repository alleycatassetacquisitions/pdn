#include "apps/controller/controller.hpp"
#include "apps/controller/controller-states.hpp"

Controller::Controller(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager)
    : TypedStateMachine<PDN>(CONTROLLER_APP_ID)
    , player(player)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , symbolWirelessManager(symbolWirelessManager) {}

Controller::~Controller() {}

void Controller::populateStateMap() {
    auto* symbolState = new SymbolState(player, remoteDeviceCoordinator, symbolWirelessManager);
    auto* symbolMatchedState = new SymbolMatchedState(player, remoteDeviceCoordinator, symbolWirelessManager);
    auto* controller1State = new Controller1State();

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
}