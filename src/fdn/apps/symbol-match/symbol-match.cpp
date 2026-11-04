#include "apps/symbol-match/symbol-match.hpp"
#include "apps/fdn-app-ids.hpp"

SymbolMatchApp::SymbolMatchApp(FDN* fdn, SymbolWirelessManager* symbolWirelessManager)
    : StateMachine(SYMBOL_MATCH_APP_ID) {
    remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    symbolManager           = new SymbolManager();
    this->symbolWirelessManager = symbolWirelessManager;
}

SymbolMatchApp::~SymbolMatchApp() {
    delete symbolManager;
    remoteDeviceCoordinator = nullptr;
    symbolWirelessManager = nullptr;
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
