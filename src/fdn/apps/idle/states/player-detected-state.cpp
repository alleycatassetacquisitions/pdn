#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"
#include "device/animation/fdn-player-detected-animation.hpp"

#define TAG "PLAYER_DETECTED"

PlayerDetectedState::PlayerDetectedState(RemotePlayerManager* remotePlayerManager,
                                         RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::PLAYER_DETECTED)
    , remotePlayerManager(remotePlayerManager) {}

void PlayerDetectedState::onStateMounted(FDN* fdn) {
    int rssi = remotePlayerManager->getLastRssi();
    const PlayerDetectionConfig& config = rssi > STRONG_RSSI_THRESHOLD   ? STRONG_CFG
                                          : rssi > MEDIUM_RSSI_THRESHOLD ? MEDIUM_CFG
                                                                         : WEAK_CFG;

    LOG_I(TAG, "Mounted — rssi %d, timeout %dms", rssi, config.timeout);

    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("PLAYER", centeredTextX("PLAYER"), 24)
        ->drawText("NEARBY", centeredTextX("NEARBY"), 40)
        ->render();

    LEDState initialState;
    static constexpr LEDColor kBlue(0, 150, 255);
    for (uint8_t i = 0; i < 9; ++i) {
        initialState.leftLights[i] = LEDState::SingleLEDState(kBlue, config.intensity);
        initialState.rightLights[i] = LEDState::SingleLEDState(kBlue, config.intensity / 2);
    }

    uint8_t frameSpeed =
    static_cast<uint8_t>(std::max(6, (255 - static_cast<int>(config.intensity)) / 7));

    AnimationConfig cfg{};
    cfg.loop         = true;
    cfg.speed        = frameSpeed;
    cfg.curve        = EaseCurve::EASE_IN_OUT;
    cfg.initialState = initialState;
    fdn->getLightManager()->startAnimation(new FDNPlayerDetectedAnimation(), cfg);

    playerDetectedTimer.setTimer(config.timeout);
}

void PlayerDetectedState::onStateLoop(FDN* fdn) {
    (void)fdn;
    remotePlayerManager->Update();
}

void PlayerDetectedState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    fdn->getLightManager()->stopAnimation();
    playerDetectedTimer.invalidate();
}

bool PlayerDetectedState::transitionToIdle() { return playerDetectedTimer.expired(); }
