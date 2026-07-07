#include "apps/symbol-lock/symbol-lock.hpp"
#include "apps/fdn-app-ids.hpp"
#include "fdn-constants.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/animation/animation-base.hpp"
#include "device/drivers/logger.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/animation/fdn-idle-animation.hpp"
#include "device/animation/fdn-player-detected-animation.hpp"

namespace {
constexpr uint8_t kIdleSpeed = 20;
static const char* TAG = "SymbolLock";

void shutdownDisplayAndLights(FDN* fdn) {
    fdn->getLightManager()->stopAnimation();
    auto* lm = static_cast<FDNLightManager*>(fdn->getLightManager());
    lm->clearAllLights(fdnNumRecessLights, fdnNumFinLights);
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT)
        ->render();
}
}

SymbolLock::SymbolLock(FDN* fdn,
                       SymbolWirelessManager* symbolWirelessManager,
                       RemotePlayerManager* remotePlayerManager,
                       bool singleSymbolMode)
    : TypedStateMachine<FDN>(SYMBOL_LOCK_APP_ID) {
    remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    symbolManager           = new SymbolLockManager(singleSymbolMode);
    this->symbolWirelessManager = symbolWirelessManager;
    this->remotePlayerManager   = remotePlayerManager;
}

SymbolLock::~SymbolLock() {
    delete symbolManager;
    remoteDeviceCoordinator = nullptr;
    symbolWirelessManager   = nullptr;
    remotePlayerManager     = nullptr;
}

void SymbolLock::onStateMounted(Device* device) {
    FDN* fdn = castDevice(device);
    remotePlayerManager->consumePacketReceived();
    nearbyAnimationTimer.invalidate();
    nearbyAnimationActive = false;
    startIdleLightAnimation(fdn);
    if (!hasLaunched()) {
        TypedStateMachine<FDN>::onStateMounted(device);
        return;
    }
    skipToState(device, kSelectionStateIndex);
}

void SymbolLock::onStateDismounted(Device* device) {
    FDN* fdn = castDevice(device);
    nearbyAnimationTimer.invalidate();
    nearbyAnimationActive = false;
    TypedStateMachine<FDN>::onStateDismounted(device);
    shutdownDisplayAndLights(fdn);
}

void SymbolLock::startIdleLightAnimation(FDN* fdn) {
    AnimationConfig cfg{};
    cfg.loop  = true;
    cfg.speed = kIdleSpeed;
    fdn->getLightManager()->startAnimation(new FDNIdleAnimation(), cfg);
}

void SymbolLock::triggerNearbyDeviceAnimation(FDN* fdn, int rssi) {
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

void SymbolLock::updateNearbyDeviceAnimation(FDN* fdn) {
    if (!nearbyAnimationActive) return;
    if (nearbyAnimationTimer.expired()) {
        nearbyAnimationTimer.invalidate();
        nearbyAnimationActive = false;
        startIdleLightAnimation(fdn);
    }
}

void SymbolLock::onStateLoop(Device* device) {
    FDN* fdn = castDevice(device);

    remotePlayerManager->Update();
    if (remotePlayerManager->consumePacketReceived()) {
        triggerNearbyDeviceAnimation(fdn, remotePlayerManager->getLastRssi());
    }
    updateNearbyDeviceAnimation(fdn);

    TypedStateMachine<FDN>::onStateLoop(device);
}

void SymbolLock::populateStateMap() {
    auto* selection = new SymbolLockSelectionState(
        symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* symbolIdle = new SymbolLockIdleState(
        symbolManager, remoteDeviceCoordinator, symbolWirelessManager);
    auto* matchSuccess = new SymbolLockMatchSuccessState(
        symbolManager, symbolWirelessManager, remoteDeviceCoordinator);

    selection->addTransition(new StateTransition(
        std::bind(&SymbolLockSelectionState::transitionToIdle, selection),
        symbolIdle));

    symbolIdle->addTransition(new StateTransition(
        std::bind(&SymbolLockIdleState::transitionToSelection, symbolIdle),
        selection));
    symbolIdle->addTransition(new StateTransition(
        std::bind(&SymbolLockIdleState::transitionToMatchSuccess, symbolIdle),
        matchSuccess));

    matchSuccess->addAppTransition(
        std::bind(&SymbolLockMatchSuccessState::transitionToFloatyBoat, matchSuccess),
        StateId(FLOATY_BOAT_APP_ID));

    stateMap.push_back(selection);    // 0
    stateMap.push_back(symbolIdle);   // 1
    stateMap.push_back(matchSuccess); // 2
}
