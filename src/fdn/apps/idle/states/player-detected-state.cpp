#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"
#include "device/animation/fdn-player-detected-animation.hpp"

#define TAG "PLAYER_DETECTED"

PlayerDetectedState::PlayerDetectedState(RemotePlayerManager* remotePlayerManager,
                                         RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::PLAYER_DETECTED)
    , remotePlayerManager(remotePlayerManager) {}

PlayerDetectedState::~PlayerDetectedState() { remotePlayerManager = nullptr; }

void PlayerDetectedState::onStateMounted(FDN* fdn) {
    int rssi = remotePlayerManager->getLastRssi();
    if      (rssi > STRONG_RSSI_THRESHOLD) activeConfig = &STRONG_CFG;
    else if (rssi > MEDIUM_RSSI_THRESHOLD) activeConfig = &MEDIUM_CFG;
    else                                   activeConfig = &WEAK_CFG;

    LOG_I(TAG, "Mounted — rssi %d, timeout %dms", rssi, activeConfig->timeout);

    pdnCabled = false;

    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("PLAYER", centeredTextX("PLAYER"), 24)
        ->drawText("NEARBY", centeredTextX("NEARBY"), 40)
        ->render();

    LEDState initialState;
    static constexpr LEDColor kBlue(0, 150, 255);
    for (uint8_t i = 0; i < 9; ++i) {
        initialState.leftLights[i]  = LEDState::SingleLEDState(kBlue, activeConfig->intensity);
        initialState.rightLights[i] = LEDState::SingleLEDState(kBlue, activeConfig->intensity / 2);
    }

    uint8_t frameSpeed = static_cast<uint8_t>(
        std::max(6, (255 - static_cast<int>(activeConfig->intensity)) / 7));

    AnimationConfig cfg{};
    cfg.loop         = true;
    cfg.speed        = frameSpeed;
    cfg.curve        = EaseCurve::EASE_IN_OUT;
    cfg.initialState = initialState;
    fdn->getLightManager()->startAnimation(new FDNPlayerDetectedAnimation(), cfg);

    playerDetectedTimer.setTimer(activeConfig->timeout);
}

void PlayerDetectedState::onStateLoop(FDN* fdn) {
    (void)fdn;
    remotePlayerManager->Update();
    pdnCabled = isConnected();
}

void PlayerDetectedState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    fdn->getLightManager()->stopAnimation();
    playerDetectedTimer.invalidate();
    pdnCabled = false;
    activeConfig       = nullptr;
}

bool PlayerDetectedState::transitionToIdle() {
    return playerDetectedTimer.expired();
}

bool PlayerDetectedState::transitionToCabledPdn() { return pdnCabled; }
