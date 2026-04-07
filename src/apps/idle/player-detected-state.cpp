#include "apps/idle/states/player-detected-state.hpp"
#include "apps/idle/idle-resources.hpp"
#include "device/drivers/logger.hpp"
#include "apps/idle/idle.hpp"
#include "utils/display-utils.hpp"
#include <algorithm>

#define TAG "PLAYER_DETECTED"

PlayerDetectedState::PlayerDetectedState(RemotePlayerManager* remotePlayerManager,
                                         HackedPlayersManager* hackedPlayersManager,
                                         FDNConnectWirelessManager* fdnConnectWirelessManager,
                                         RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState(remoteDeviceCoordinator, IdleStateId::PLAYER_DETECTED) {
    this->remotePlayerManager = remotePlayerManager;
    this->hackedPlayersManager = hackedPlayersManager;
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
}

PlayerDetectedState::~PlayerDetectedState() {
    remotePlayerManager = nullptr;
    hackedPlayersManager = nullptr;
    fdnConnectWirelessManager = nullptr;
}

void PlayerDetectedState::setConnectionHandler(
    std::function<void(const std::string&, const uint8_t*)> handler) {
    connectionHandler = std::move(handler);
}

void PlayerDetectedState::onStateMounted(Device* PDN) {
    int rssi = remotePlayerManager->getLastRssi();
    if (rssi > -50)      activeConfig = &STRONG;
    else if (rssi > -70) activeConfig = &MEDIUM;
    else                 activeConfig = &WEAK;

    LOG_I(TAG, "Mounted — rssi %d, strength %d, timeout %dms", rssi, (int)activeConfig->strength, activeConfig->timeout);

    PDN->getLightManager()->setGlobalBrightness(activeConfig->intensity);
    connectionResolved = false;
    contentReady = false;

    fdnConnectWirelessManager->setConnectCallback(
        [this](const std::string& playerId, const uint8_t* senderMac) {
            LOG_I(TAG, "PDN connected, player: %s", playerId.c_str());
            fdnConnectWirelessManager->setPeer(senderMac);
            if (connectionHandler) connectionHandler(playerId, senderMac);
            connectionResolved = true;
        }
    );

    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(PDN);
}

void PlayerDetectedState::onStateLoop(Device* PDN) {
    remotePlayerManager->Update();

    if (isInGlyphLoadingPhase(PDN, glyphTimer)) return;

    if (!contentReady) {
        PDN->getDisplay()->invalidateScreen()->render();

        LEDState initialState;
        for (uint8_t i = 0; i < 23; i++) {
            initialState.setRecessLight(i, LEDColor(), activeConfig->intensity);
        }
        for (uint8_t i = 0; i < 9; i++) {
            initialState.setFinLight(i, LEDColor(), activeConfig->intensity);
        }

        uint8_t frameSpeed = static_cast<uint8_t>(std::max(8, (255 - activeConfig->intensity) / 7));

        AnimationConfig cfg;
        cfg.type         = AnimationType::PLAYER_DETECTED;
        cfg.loop         = true;
        cfg.speed        = frameSpeed;
        cfg.curve        = EaseCurve::EASE_IN_OUT;
        cfg.initialState = initialState;
        PDN->getLightManager()->startAnimation(cfg);

        playerDetectedTimer.setTimer(activeConfig->timeout);
        contentReady = true;
    }
}

void PlayerDetectedState::onStateDismounted(Device* PDN) {
    LOG_I(TAG, "Dismounted");
    fdnConnectWirelessManager->clearCallbacks();
    PDN->getLightManager()->stopAnimation();
    glyphTimer.invalidate();
    playerDetectedTimer.invalidate();
    connectionResolved = false;
    contentReady = false;
    activeConfig = nullptr;
}

bool PlayerDetectedState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}

bool PlayerDetectedState::transitionToIdle() {
    return playerDetectedTimer.expired();
}

bool PlayerDetectedState::transitionToConnectionDetected() {
    return connectionResolved;
}
