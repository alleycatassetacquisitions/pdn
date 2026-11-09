#include "apps/symbol-match/symbol-match.hpp"
#include "apps/fdn-app-ids.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"

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
    auto* selection = new Selection(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* symbolIdle = new SymbolIdle(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* matchSuccess = new MatchSuccess(remoteDeviceCoordinator, symbolWirelessManager);

    // The game ends once no registered PDN remains, so an unregistered one left behind can
    // register; MatchSuccess holds its codeword first. The debounce rides out a cable jiggle.
    FdnProvisioningManager* provisioning = fdnProvisioningManager;
    auto gameOver = [provisioning](FDNConnectState* state) {
        return state->isPersistentlyDisconnected() ||
               (state->isConnected() && !provisioning->hasRegisteredPdnCabled());
    };
    selection->addAppTransition([selection, gameOver]() { return gameOver(selection); },
                                StateId(IDLE_APP_ID));
    symbolIdle->addAppTransition([symbolIdle, gameOver]() { return gameOver(symbolIdle); },
                                 StateId(IDLE_APP_ID));

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
