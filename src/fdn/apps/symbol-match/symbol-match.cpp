#include "apps/symbol-match/symbol-match.hpp"
#include "apps/fdn-app-ids.hpp"

SymbolMatchApp::SymbolMatchApp(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                               SymbolWirelessManager* symbolWirelessManager)
    : StateMachine(SYMBOL_MATCH_APP_ID)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , symbolManager(new SymbolManager())
    , symbolWirelessManager(symbolWirelessManager) {}

SymbolMatchApp::~SymbolMatchApp() {
    delete symbolManager;
}

void SymbolMatchApp::populateStateMap() {
    auto* selection = new Selection(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* symbolIdle = new SymbolIdle(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* matchSuccess = new MatchSuccess(remoteDeviceCoordinator, symbolWirelessManager);

    selection->addTransition(
    new StateTransition([selection]() { return selection->transitionToIdle(); }, symbolIdle));

    symbolIdle->addTransition(
    new StateTransition([symbolIdle]() { return symbolIdle->transitionToSelection(); }, selection));
    symbolIdle->addTransition(new StateTransition(
    [symbolIdle]() { return symbolIdle->transitionToMatchSuccess(); }, matchSuccess));

    matchSuccess->addTransition(new StateTransition(
    [matchSuccess]() { return matchSuccess->transitionToSelection(); }, selection));

    stateMap.push_back(selection);
    stateMap.push_back(symbolIdle);
    stateMap.push_back(matchSuccess);
}
