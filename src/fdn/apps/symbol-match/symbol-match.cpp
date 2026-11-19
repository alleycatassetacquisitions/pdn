#include "apps/symbol-match/symbol-match.hpp"
#include "apps/fdn-app-ids.hpp"

SymbolMatchApp::SymbolMatchApp(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                               SymbolWirelessManager* symbolWirelessManager,
                               FdnProvisioningManager* fdnProvisioningManager)
    : StateMachine(SYMBOL_MATCH_APP_ID)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , symbolManager(new SymbolManager())
    , symbolWirelessManager(symbolWirelessManager)
    , fdnProvisioningManager(fdnProvisioningManager) {}

SymbolMatchApp::~SymbolMatchApp() {
    delete symbolManager;
}

void SymbolMatchApp::populateStateMap() {
    auto* selection = new Selection(symbolManager, remoteDeviceCoordinator, symbolWirelessManager,
                                    fdnProvisioningManager);
    auto* symbolIdle = new SymbolIdle(symbolManager, remoteDeviceCoordinator, symbolWirelessManager,
                                      fdnProvisioningManager);
    auto* matchSuccess = new MatchSuccess(remoteDeviceCoordinator, symbolWirelessManager);
    auto* registration = new Registration(fdnProvisioningManager);

    selection->addTransition(new StateTransition(
    [selection]() { return selection->transitionToRegistration(); }, registration));
    selection->addTransition(
    new StateTransition([selection]() { return selection->transitionToIdle(); }, symbolIdle));

    symbolIdle->addTransition(new StateTransition(
    [symbolIdle]() { return symbolIdle->transitionToRegistration(); }, registration));
    symbolIdle->addTransition(
    new StateTransition([symbolIdle]() { return symbolIdle->transitionToSelection(); }, selection));
    symbolIdle->addTransition(new StateTransition(
    [symbolIdle]() { return symbolIdle->transitionToMatchSuccess(); }, matchSuccess));

    matchSuccess->addTransition(new StateTransition(
    [matchSuccess]() { return matchSuccess->transitionToSelection(); }, selection));

    registration->addTransition(new StateTransition(
    [registration]() { return registration->transitionToSelection(); }, selection));

    stateMap.push_back(selection);
    stateMap.push_back(symbolIdle);
    stateMap.push_back(matchSuccess);
    stateMap.push_back(registration);
}
