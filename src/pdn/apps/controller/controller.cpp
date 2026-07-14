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
    // On re-entry, always restart symbol matching — the FDN resets to
    // symbol-lock after each session, so the PDN must go through symbol
    // exchange again. Use skipToState so populateStateMap is NOT called
    // a second time (it would duplicate every state in the map).
    //
    // Also reset the disconnect debounce. When the previous session ended via
    // a disconnect, the debounce timer is left in an elapsed state. If the FDN
    // port isn't yet CONNECTED in the RDC on the first loop tick after mount,
    // isPersistentlyDisconnected() would immediately return true (the timer is
    // already expired) and kick the app back to QUICKDRAW before the FDN can
    // re-register, causing rapid mount/dismount cycles on reconnection.
    disconnectPolicy.resetDebounce();
    skipToState(device, kSymbolStateIndex);
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