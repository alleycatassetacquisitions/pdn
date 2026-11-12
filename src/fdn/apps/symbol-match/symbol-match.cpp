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

    // The game ends once no PDN remains cabled. MatchSuccess holds its codeword first; one
    // window spans Selection and SymbolIdle so their swap doesn't restart it.
    RemoteDeviceCoordinator* rdc = remoteDeviceCoordinator;
    DebouncedCondition* debounce = &gameOverDebounce;
    auto gameOver = [rdc, debounce]() {
        return debounce->heldFor(rdc->getConnectedPeerMac(DeviceType::PDN) == nullptr,
                                 GAME_OVER_DEBOUNCE_MS);
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
