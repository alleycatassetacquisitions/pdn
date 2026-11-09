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
    // register; MatchSuccess holds its codeword first. One window spans both states, so a
    // jiggled cable never ends a round.
    FdnProvisioningManager* provisioning = fdnProvisioningManager;
    DebouncedCondition* debounce = &gameOverDebounce;
    auto gameOver = [provisioning, debounce]() {
        return debounce->heldFor(!provisioning->hasRegisteredPdnCabled(), GAME_OVER_DEBOUNCE_MS);
    };
    selection->addAppTransition(gameOver, StateId(IDLE_APP_ID));
    symbolIdle->addAppTransition(gameOver, StateId(IDLE_APP_ID));

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
