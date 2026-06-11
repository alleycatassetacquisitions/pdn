#include "apps/symbol-match/symbol-match-states.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "SYMBOL_CONN_DETECTED"

SymbolMatchConnectionDetectedState::SymbolMatchConnectionDetectedState(
    HackedPlayersManager* hackedPlayersManager,
    RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, SymbolMatchStateId::SYMBOL_MATCH_CONNECTION_DETECTED)
    , hackedPlayersManager(hackedPlayersManager) {}

SymbolMatchConnectionDetectedState::~SymbolMatchConnectionDetectedState() {
    hackedPlayersManager = nullptr;
}

void SymbolMatchConnectionDetectedState::receivePdnConnection(const std::string& playerId) {
    pendingPlayerId = playerId;
}

void SymbolMatchConnectionDetectedState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted - player: %s", pendingPlayerId.c_str());
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void SymbolMatchConnectionDetectedState::onStateLoop(FDN* fdn) {
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        wasHacked = hackedPlayersManager->hasPlayerHacked(pendingPlayerId);
        LOG_I(TAG, "Player %s hacked: %s", pendingPlayerId.c_str(), wasHacked ? "yes" : "no");
        fdn->getDisplay()->invalidateScreen()->render();
        contentReady = true;
    }
}

void SymbolMatchConnectionDetectedState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    glyphTimer.invalidate();
    contentReady = false;
    wasHacked    = false;
    pendingPlayerId.clear();
}

bool SymbolMatchConnectionDetectedState::transitionToAuth() {
    return contentReady && wasHacked;
}

bool SymbolMatchConnectionDetectedState::transitionToUnauthorized() {
    return contentReady && !wasHacked;
}

bool SymbolMatchConnectionDetectedState::transitionToSelection() {
    return !isConnected();
}
