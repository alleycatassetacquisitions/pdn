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
    SymbolIdle* symbolIdle = new SymbolIdle(symbolManager, remoteDeviceCoordinator);
    LeftConnected* leftConnected = new LeftConnected(symbolManager, remoteDeviceCoordinator);
    RightConnected* rightConnected = new RightConnected(symbolManager, remoteDeviceCoordinator);
    BothConnected* bothConnected = new BothConnected(symbolManager, remoteDeviceCoordinator);

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
            std::bind(&SymbolIdle::transitionToLeftConnected, symbolIdle),
            leftConnected));

    symbolIdle->addTransition(
        new StateTransition(
            std::bind(&SymbolIdle::transitionToRightConnected, symbolIdle),
            rightConnected));

    leftConnected->addTransition(
        new StateTransition(
            std::bind(&LeftConnected::transitionToSelection, leftConnected),
            selection));

    leftConnected->addTransition(
        new StateTransition(
            std::bind(&LeftConnected::transitionToSymbolIdle, leftConnected),
            symbolIdle));

    leftConnected->addTransition(
        new StateTransition(
            std::bind(&LeftConnected::transitionToBothConnected, leftConnected),
            bothConnected));

    rightConnected->addTransition(
        new StateTransition(
            std::bind(&RightConnected::transitionToSelection, rightConnected),
            selection));

    rightConnected->addTransition(
        new StateTransition(
            std::bind(&RightConnected::transitionToSymbolIdle, rightConnected),
            symbolIdle));

    rightConnected->addTransition(
        new StateTransition(
            std::bind(&RightConnected::transitionToBothConnected, rightConnected),
            bothConnected));

    bothConnected->addTransition(
        new StateTransition(
            std::bind(&BothConnected::transitionToSelection, bothConnected),
            selection));

    bothConnected->addTransition(
        new StateTransition(
            std::bind(&BothConnected::transitionToLeftConnected, bothConnected),
            leftConnected));

    bothConnected->addTransition(
        new StateTransition(
            std::bind(&BothConnected::transitionToRightConnected, bothConnected),
            rightConnected));

    stateMap.push_back(selection);
    stateMap.push_back(symbolIdle);
}