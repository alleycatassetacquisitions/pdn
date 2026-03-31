#include "../../include/symbol-match/symbol-match.hpp"

SymbolMatch::SymbolMatch(Device* FDN) : StateMachine(SYMBOLMATCH_APP_ID) {
    this->remoteDeviceCoordinator = FDN->getRemoteDeviceCoordinator();
    this->symbolManager = new SymbolManager();
}

SymbolMatch::~SymbolMatch() {
    remoteDeviceCoordinator = nullptr;
    delete symbolManager;
}

void SymbolMatch::populateStateMap() {

    Selection* selection = new Selection(symbolManager);
    SymbolIdle* idle = new SymbolIdle(symbolManager, remoteDeviceCoordinator);

    selection->addTransition(
        new StateTransition(
            std::bind(&Selection::transitionToIdle, selection),
            idle));

    idle->addTransition(
        new StateTransition(
            std::bind(&SymbolIdle::transitionToSelection, idle),
            selection));

    stateMap.push_back(selection);
    stateMap.push_back(idle);
}