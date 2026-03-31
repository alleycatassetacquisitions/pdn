#include "../../include/symbol-match/symbol-match.hpp"

SymbolMatch::SymbolMatch(Device* FDN, SymbolWirelessManager* symbolWirelessManager) : StateMachine(SYMBOLMATCH_APP_ID) {
    this->remoteDeviceCoordinator = FDN->getRemoteDeviceCoordinator();
    this->symbolManager = new SymbolManager();
    this->symbolWirelessManager = symbolWirelessManager;
}

SymbolMatch::~SymbolMatch() {
    remoteDeviceCoordinator = nullptr;
    delete symbolManager;
    symbolWirelessManager = nullptr;
}

void SymbolMatch::populateStateMap() {

    Selection* selection = new Selection(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    SymbolIdle* symbolIdle = new SymbolIdle(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    MatchSuccess* matchSuccess = new MatchSuccess(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);

    selection->addTransition(
        new StateTransition(
            std::bind(&Selection::transitionToIdle, selection),
            symbolIdle));

    symbolIdle->addTransition(
        new StateTransition(
            std::bind(&SymbolIdle::transitionToSelection, symbolIdle),
            selection));

    symbolIdle->addTransition(
        new StateTransition(
            std::bind(&SymbolIdle::transitionToMatchSuccess, symbolIdle),
            matchSuccess));

    matchSuccess->addTransition(
        new StateTransition(
            std::bind(&MatchSuccess::transitionToSelection, matchSuccess),
            selection));

    stateMap.push_back(selection);
    stateMap.push_back(symbolIdle);
    stateMap.push_back(matchSuccess);
}
