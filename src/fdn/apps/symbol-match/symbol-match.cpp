#include "apps/symbol-match/symbol-match.hpp"
#include "apps/fdn-app-ids.hpp"
#include "wireless/remote-player-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "device/animation/animation-base.hpp"
#include "device/drivers/logger.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/animation/fdn-idle-animation.hpp"
#include "device/animation/fdn-player-detected-animation.hpp"
#include <algorithm>

namespace {
constexpr uint8_t kIdleSpeed = 20;
static const char* TAG = "SymbolMatch";
}

SymbolMatch::SymbolMatch(FDN* fdn,
                         SymbolWirelessManager* symbolWirelessManager,
                         RemotePlayerManager* remotePlayerManager,
                         HackedPlayersManager* hackedPlayersManager,
                         FDNConnectWirelessManager* fdnConnectWirelessManager)
    : StateMachine(SYMBOL_MATCH_APP_ID) {
    remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    symbolManager           = new SymbolManager();
    this->symbolWirelessManager   = symbolWirelessManager;
    this->remotePlayerManager     = remotePlayerManager;
    this->hackedPlayersManager    = hackedPlayersManager;
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
}

SymbolMatch::~SymbolMatch() {
    delete symbolManager;
    remoteDeviceCoordinator = nullptr;
    symbolWirelessManager   = nullptr;
    remotePlayerManager     = nullptr;
    hackedPlayersManager    = nullptr;
    fdnConnectWirelessManager = nullptr;
}

void SymbolMatch::onStateMounted(Device* device) {
    FDN* fdn = static_cast<FDN*>(device);
    remotePlayerManager->consumePacketReceived();
    connectionResolved = false;
    pendingConnectedPlayerId.clear();
    fdnConnectWirelessManager->setConnectCallback(
        [this](const std::string& playerId, const uint8_t* senderMac) {
            LOG_I(TAG, "PDN connected, player: %s", playerId.c_str());
            fdnConnectWirelessManager->setPeer(senderMac);
            pendingConnectedPlayerId = playerId;
            connectionResolved = true;
        });
    nearbyAnimationTimer.invalidate();
    nearbyAnimationActive = false;
    startIdleLightAnimation(fdn);
    StateMachine::onStateMounted(device);
}

// std::unique_ptr<Snapshot> SymbolMatch::onStatePaused(Device* device) {
//     uploadCheckTimer.invalidate();
//     nearbyAnimationTimer.invalidate();
//     nearbyAnimationActive = false;
//     return StateMachine::onStatePaused(device);
// }

// void SymbolMatch::onStateResumed(Device* device, Snapshot* snapshot) {
//     StateMachine::onStateResumed(device, snapshot);
//
//     FDN* fdn = static_cast<FDN*>(device);
//     connectionResolved = false;
//     pendingConnectedPlayerId.clear();
//     uploadCheckTimer.invalidate();
//     nearbyAnimationTimer.invalidate();
//     nearbyAnimationActive = false;
//
//     skipToState(device, kSelectionStateIndex);
//     startIdleLightAnimation(fdn);
// }

void SymbolMatch::onStateDismounted(Device* device) {
    FDN* fdn = static_cast<FDN*>(device);
    uploadCheckTimer.invalidate();
    fdnConnectWirelessManager->clearCallbacks();
    connectionResolved = false;
    pendingConnectedPlayerId.clear();
    nearbyAnimationTimer.invalidate();
    nearbyAnimationActive = false;
    fdn->getLightManager()->stopAnimation();
    StateMachine::onStateDismounted(device);
}

bool SymbolMatch::shouldTransitionToUploadPending() {
    return uploadCheckTimer.expired() && !hackedPlayersManager->getPendingUploads().empty();
}

void SymbolMatch::startIdleLightAnimation(FDN* fdn) {
    AnimationConfig cfg{};
    cfg.loop  = true;
    cfg.speed = kIdleSpeed;
    fdn->getLightManager()->startAnimation(new FDNIdleAnimation(), cfg);
}

void SymbolMatch::triggerNearbyDeviceAnimation(FDN* fdn, int rssi) {
    uint8_t intensity  = 80;
    int     timeoutMs  = 2500;
    uint8_t frameSpeed = 12;

    if (rssi > STRONG_RSSI_THRESHOLD) {
        intensity  = 255;
        timeoutMs  = 5000;
        frameSpeed = 6;
    } else if (rssi > MEDIUM_RSSI_THRESHOLD) {
        intensity  = 150;
        frameSpeed = 9;
    }

    LEDState initialState;
    static constexpr LEDColor kColor(0, 200, 255);
    for (uint8_t i = 0; i < 9; ++i) {
        initialState.leftLights[i]  = LEDState::SingleLEDState(kColor, intensity);
        initialState.rightLights[i] = LEDState::SingleLEDState(kColor, intensity / 2);
    }

    AnimationConfig cfg{};
    cfg.loop         = true;
    cfg.speed        = frameSpeed;
    cfg.curve        = EaseCurve::EASE_IN_OUT;
    cfg.initialState = initialState;
    fdn->getLightManager()->startAnimation(new FDNPlayerDetectedAnimation(), cfg);

    nearbyAnimationTimer.setTimer(timeoutMs);
    nearbyAnimationActive = true;
}

void SymbolMatch::updateNearbyDeviceAnimation(FDN* fdn) {
    if (!nearbyAnimationActive) return;
    if (nearbyAnimationTimer.expired()) {
        nearbyAnimationTimer.invalidate();
        nearbyAnimationActive = false;
        startIdleLightAnimation(fdn);
    }
}

void SymbolMatch::onStateLoop(Device* device) {
    FDN* fdn = static_cast<FDN*>(device);

    remotePlayerManager->Update();
    if (remotePlayerManager->consumePacketReceived()) {
        triggerNearbyDeviceAnimation(fdn, remotePlayerManager->getLastRssi());
    }
    updateNearbyDeviceAnimation(fdn);

    if (connectionResolved && currentState &&
        (currentState->getStateId() == SymbolMatchStateId::SELECTION ||
         currentState->getStateId() == SymbolMatchStateId::SYMBOL_IDLE)) {
        auto* connectionDetected =
            static_cast<SymbolMatchConnectionDetectedState*>(stateMap[kConnectionDetectedStateIndex]);
        connectionDetected->receivePdnConnection(pendingConnectedPlayerId);
        skipToState(device, kConnectionDetectedStateIndex);
        connectionResolved = false;
        return;
    }

    if (currentState && currentState->getStateId() == SymbolMatchStateId::SELECTION) {
        if (!uploadCheckTimer.isRunning()) {
            uploadCheckTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
        }
        if (shouldTransitionToUploadPending()) {
            const auto pending = hackedPlayersManager->getPendingUploads();
            LOG_I(TAG, "Upload: %u pending hack(s) queued",
                  static_cast<unsigned>(pending.size()));
            skipToState(device, kUploadPendingStateIndex);
            uploadCheckTimer.invalidate();
            return;
        }
    } else {
        uploadCheckTimer.invalidate();
    }

    StateMachine::onStateLoop(device);
}

void SymbolMatch::populateStateMap() {
    auto* selection = new Selection(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* symbolIdle = new SymbolIdle(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* matchSuccess = new MatchSuccess(symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* uploadPending = new SymbolMatchUploadPendingHacksState(hackedPlayersManager);
    auto* connectionDetected = new SymbolMatchConnectionDetectedState(hackedPlayersManager, remoteDeviceCoordinator);
    auto* authDetected = new SymbolMatchAuthDetectedState(remoteDeviceCoordinator, fdnConnectWirelessManager);
    auto* unauthorizedDetected = new SymbolMatchUnauthorizedDetectedState(remoteDeviceCoordinator);

    uploadPending->addTransition(new StateTransition(
        std::bind(&SymbolMatchUploadPendingHacksState::transitionToIdle, uploadPending),
        selection));

    connectionDetected->addTransition(new StateTransition(
        std::bind(&SymbolMatchConnectionDetectedState::transitionToSelection, connectionDetected),
        selection));
    connectionDetected->addTransition(new StateTransition(
        std::bind(&SymbolMatchConnectionDetectedState::transitionToAuth, connectionDetected),
        authDetected));
    connectionDetected->addAppTransition(
        std::bind(&SymbolMatchConnectionDetectedState::transitionToUnauthorized, connectionDetected),
        StateId(HACKING_APP_ID));

    authDetected->addTransition(new StateTransition(
        std::bind(&SymbolMatchAuthDetectedState::transitionToSelection, authDetected),
        selection));
    authDetected->addAppTransition(
        std::bind(&SymbolMatchAuthDetectedState::transitionToMainMenu, authDetected),
        StateId(MAIN_MENU_APP_ID));

    unauthorizedDetected->addTransition(new StateTransition(
        std::bind(&SymbolMatchUnauthorizedDetectedState::transitionToSelection, unauthorizedDetected),
        selection));
    unauthorizedDetected->addAppTransition(
        std::bind(&SymbolMatchUnauthorizedDetectedState::transitionToHacking, unauthorizedDetected),
        StateId(HACKING_APP_ID));

    selection->addTransition(new StateTransition(
        std::bind(&Selection::transitionToIdle, selection),
        symbolIdle));

    symbolIdle->addTransition(new StateTransition(
        std::bind(&SymbolIdle::transitionToSelection, symbolIdle),
        selection));
    symbolIdle->addAppTransition(
        std::bind(&SymbolIdle::transitionToMainMenu, symbolIdle),
        StateId(MAIN_MENU_APP_ID));
    symbolIdle->addTransition(new StateTransition(
        std::bind(&SymbolIdle::transitionToMatchSuccess, symbolIdle),
        matchSuccess));

    matchSuccess->addAppTransition(
        std::bind(&MatchSuccess::transitionToMainMenu, matchSuccess),
        StateId(MAIN_MENU_APP_ID));

    stateMap.push_back(selection);          // 0
    stateMap.push_back(symbolIdle);         // 1
    stateMap.push_back(matchSuccess);       // 2
    stateMap.push_back(uploadPending);      // 3
    stateMap.push_back(connectionDetected); // 4
    stateMap.push_back(authDetected);       // 5
    stateMap.push_back(unauthorizedDetected); // 6
}
