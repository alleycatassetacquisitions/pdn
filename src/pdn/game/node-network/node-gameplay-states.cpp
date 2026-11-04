#include <cstdio>
#include <cstring>
#include <string>

#include "game/node-network/node-network-states.hpp"

#include "alleycat-server/matches-api.hpp"
#include "device/animation/countdown-animation.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/transmit-breath-animation.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include "game/node-network/node-network-resources.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-states.hpp"

#define TAG "NODE_GPLAY"

// --- NodeDuelCountdown ---

NodeDuelCountdown::NodeDuelCountdown(MatchManagerV2* matchManager,
                                     RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NODE_DUEL_COUNTDOWN)
    , matchManager(matchManager) {}

NodeDuelCountdown::~NodeDuelCountdown() {
    matchManager = nullptr;
}

NodeDuelCountdown::CountdownStage NodeDuelCountdown::stageAt(int index) {
    static constexpr CountdownStage QUEUE[4] = {
        {CountdownStep::THREE, 2000},
        {CountdownStep::TWO, 2000},
        {CountdownStep::ONE, 2000},
        {CountdownStep::BATTLE, 0},
    };
    return QUEUE[index];
}

ImageType NodeDuelCountdown::imageFor(CountdownStep step) {
    switch (step) {
        case CountdownStep::THREE:
            return ImageType::COUNTDOWN_THREE;
        case CountdownStep::TWO:
            return ImageType::COUNTDOWN_TWO;
        default:
            return ImageType::COUNTDOWN_ONE;
    }
}

AnimationConfig NodeDuelCountdown::animationConfigFor(CountdownStep step) {
    AnimationConfig config;
    config.loop = false;
    config.speed = 16;
    switch (step) {
        case CountdownStep::THREE:
            config.initialState = COUNTDOWN_THREE_STATE;
            break;
        case CountdownStep::TWO:
            config.initialState = COUNTDOWN_TWO_STATE;
            break;
        case CountdownStep::ONE:
            config.initialState = COUNTDOWN_ONE_STATE;
            break;
        case CountdownStep::BATTLE:
            config.initialState = COUNTDOWN_DUEL_STATE;
            break;
    }
    return config;
}

void NodeDuelCountdown::showStep(PDN* pdn, const CountdownStage& stage) {
    pdn->getDisplay()->invalidateScreen()->drawImage(nodeNetworkImage(imageFor(stage.step)))->render();
    // Each step restarts the animation with its own initial state, as Quickdraw's countdown does.
    pdn->getLightManager()->startAnimation(new CountdownAnimation(), animationConfigFor(stage.step));
    countdownTimer.setTimer(stage.countdownTimer);
    currentStepIndex++;
    pdn->getHaptics()->setIntensity(HAPTIC_INTENSITY);
    hapticTimer.setTimer(HAPTIC_DURATION);
}

void NodeDuelCountdown::onStateMounted(PDN* pdn) {
    currentStepIndex = 0;
    doBattle = false;
    showStep(pdn, stageAt(0));
}

void NodeDuelCountdown::onStateLoop(PDN* pdn) {
    countdownTimer.updateTime();
    hapticTimer.updateTime();
    if (hapticTimer.expired()) {
        pdn->getHaptics()->setIntensity(0);
    }
    if (!countdownTimer.expired() || currentStepIndex >= 4) {
        return;
    }
    const CountdownStage stage = stageAt(currentStepIndex);
    if (stage.step == CountdownStep::BATTLE) {
        doBattle = true;
        return;
    }
    showStep(pdn, stage);
}

void NodeDuelCountdown::onStateDismounted(PDN* pdn) {
    if (!doBattle) {
        matchManager->clearCurrentMatch();
    }
    doBattle = false;
    currentStepIndex = 0;
    countdownTimer.invalidate();
    pdn->getLightManager()->stopAnimation();
    pdn->getHaptics()->setIntensity(0);
}

bool NodeDuelCountdown::shallWeBattle() {
    return doBattle;
}

bool NodeDuelCountdown::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool NodeDuelCountdown::isPrimaryRequired() {
    return matchManager->getLocalLinkSide() == DuelLinkSide::INITIATOR;
}

bool NodeDuelCountdown::isAuxRequired() {
    return matchManager->getLocalLinkSide() == DuelLinkSide::RESPONDER;
}

// --- NodeDuel ---

NodeDuel::NodeDuel(MatchManagerV2* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NODE_DUEL)
    , matchManager(matchManager) {}

NodeDuel::~NodeDuel() {
    matchManager = nullptr;
}

void NodeDuel::onDrawButton(PDN* pdn) {
    const unsigned long nowMs = SimpleTimer::getPlatformClock()->milliseconds();
    const uint64_t elapsed = nowMs > duelStartMs ? nowMs - duelStartMs : 0;
    matchManager->onLocalDrawTimeMs(elapsed);
}

void NodeDuel::onStateMounted(PDN* pdn) {
    outcomeReady = false;
    syncSuccess = false;
    duelStartMs = SimpleTimer::getPlatformClock()->milliseconds();
    duelTimer.setTimer(DUEL_TIMEOUT);

    parameterizedCallbackFunction drawPress = [](void* ctx) {
        NodeDuel* duel = static_cast<NodeDuel*>(ctx);
        duel->onDrawButton(nullptr);
    };
    pdn->getPrimaryButton()->setButtonPress(drawPress, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(drawPress, this, ButtonInteraction::CLICK);

    pdn->getDisplay()
        ->invalidateScreen()
        ->drawImage(nodeNetworkImage(ImageType::IDLE))
        ->drawImage(nodeNetworkImage(ImageType::DRAW))
        ->render();

    AnimationConfig config;
    config.speed = 16;
    config.loop = false;
    config.initialState = COUNTDOWN_DUEL_STATE;
    pdn->getLightManager()->startAnimation(new CountdownAnimation(), config);
    pdn->getHaptics()->setIntensity(175);
}

void NodeDuel::onStateLoop(PDN* pdn) {
    duelTimer.updateTime();

    if (matchManager->isExchangeComplete() && !outcomeReady) {
        outcomeReady = true;
        syncSuccess = matchManager->didSync();
        LOG_I(TAG, "duel exchange complete: sync=%d", syncSuccess ? 1 : 0);
        if (syncSuccess) {
            matchManager->finalizeMatch();
            matchManager->recordLinkOutcome(true);
        } else {
            matchManager->recordLinkOutcome(false);
            LOG_W(TAG, "duel failed, retries left=%lu",
                  static_cast<unsigned long>(matchManager->getRetriesRemaining()));
            matchManager->clearCurrentMatch();
        }
        return;
    }

    if (duelTimer.expired() && !matchManager->getSession().hasLocalDrawTime()) {
        const unsigned long elapsed = SimpleTimer::getPlatformClock()->milliseconds() - duelStartMs;
        matchManager->onLocalGraceExpired(elapsed);
    }
}

void NodeDuel::onStateDismounted(PDN* pdn) {
    duelTimer.invalidate();
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getLightManager()->stopAnimation();
    pdn->getHaptics()->setIntensity(0);
    outcomeReady = false;
}

bool NodeDuel::transitionToWin() {
    return outcomeReady && syncSuccess;
}

bool NodeDuel::transitionToLinkRetry() {
    return outcomeReady && !syncSuccess;
}

bool NodeDuel::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool NodeDuel::isPrimaryRequired() {
    return matchManager->getLocalLinkSide() == DuelLinkSide::INITIATOR;
}

bool NodeDuel::isAuxRequired() {
    return matchManager->getLocalLinkSide() == DuelLinkSide::RESPONDER;
}

// --- NodeWin ---

NodeWin::NodeWin(MatchManagerV2* matchManager) : TypedState<PDN>(NODE_WIN), matchManager(matchManager) {}

NodeWin::~NodeWin() {
    matchManager = nullptr;
}

void NodeWin::onStateMounted(PDN* pdn) {
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    pdn->getDisplay()->invalidateScreen()->
    drawImage(nodeNetworkImage(ImageType::LOGO_LEFT))->
    drawImage(nodeNetworkImage(ImageType::WIN))->render();

    winTimer.setTimer(3000);

    AnimationConfig config;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;
    pdn->getLightManager()->startAnimation(new HunterWinAnimation(nodeNetworkWinLEDColors), config);
}

void NodeWin::onStateLoop(PDN* pdn) {
    winTimer.updateTime();
}

void NodeWin::onStateDismounted(PDN* pdn) {
    winTimer.invalidate();
    pdn->getLightManager()->stopAnimation();
}

bool NodeWin::transitionToUpload() {
    return winTimer.expired();
}

// --- NodeLinkRetry ---

NodeLinkRetry::NodeLinkRetry(MatchManagerV2* matchManager)
    : TypedState<PDN>(NODE_LINK_RETRY), matchManager(matchManager) {}

NodeLinkRetry::~NodeLinkRetry() {
    matchManager = nullptr;
}

void NodeLinkRetry::onStateMounted(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen()->drawImage(nodeNetworkImage(ImageType::IDLE))->render();
    pdn->getDisplay()
        ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawText("Retries", 70, 20)
        ->drawText("Left", 70, 35);
    const std::string retriesText = std::to_string(matchManager->getRetriesRemaining());
    pdn->getDisplay()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawText(retriesText.c_str(), 80, 55);
    pdn->getDisplay()->render();
    displayTimer.setTimer(2500);
}

void NodeLinkRetry::onStateLoop(PDN* pdn) {
    displayTimer.updateTime();
}

void NodeLinkRetry::onStateDismounted(PDN* pdn) {
    displayTimer.invalidate();
}

bool NodeLinkRetry::transitionToIdle() {
    return displayTimer.expired() && !matchManager->hasExhaustedAttempts();
}

bool NodeLinkRetry::transitionToSleep() {
    return displayTimer.expired() && matchManager->hasExhaustedAttempts();
}

// --- NodeUploadMatches ---

NodeUploadMatches::NodeUploadMatches(WirelessManager* wirelessManager, MatchManagerV2* matchManager,
                                     NodeNetworkStats* stats)
    : TypedState<PDN>(NODE_UPLOAD_MATCHES)
    , wirelessManager(wirelessManager)
    , matchManager(matchManager)
    , stats(stats) {}

NodeUploadMatches::~NodeUploadMatches() {
    wirelessManager = nullptr;
    matchManager = nullptr;
    stats = nullptr;
}

void NodeUploadMatches::attemptUpload(PDN* pdn) {
    const size_t count = matchManager->getPendingRecordCount();
    LOG_I(TAG, "upload: attempt %d, pending=%lu, wifiConnected=%d", retryCount + 1,
          static_cast<unsigned long>(count), wirelessManager->isWifiConnected() ? 1 : 0);
    if (count == 0) {
        LOG_W(TAG, "upload: nothing pending (record was not stored?), going to sleep");
        transitionToSleepState = true;
        return;
    }

    QuickdrawMatchCodec::MatchSource source = [this](size_t index, QuickdrawMatchRecord& record) {
        return matchManager->readPendingRecord(index, record);
    };

    MatchesApi::uploadQuickdrawMatches(
        wirelessManager, count, source,
        [](const QuickdrawMatchRejection& rejection) {
            LOG_W(TAG, "upload: server rejected match index=%lu code=%d raw=%ld field=%s",
                  static_cast<unsigned long>(rejection.matchIndex), static_cast<int>(rejection.code),
                  static_cast<long>(rejection.rawCode),
                  rejection.fieldCount > 0 ? rejection.fields[0] : "-");
        },
        [this](const QuickdrawUploadResult& result) {
            LOG_I(TAG, "upload: response ok, rejections=%lu fullyAccepted=%d",
                  static_cast<unsigned long>(result.rejectionCount),
                  result.isFullyAccepted() ? 1 : 0);
            if (stats != nullptr) {
                const bool counted = stats->recordSuccessfulUploadPartner(uploadPeerMac);
                LOG_I(TAG, "upload: partner %s %s, nodesCreated=%lu", MacToString(uploadPeerMac),
                      counted ? "counted" : "already counted",
                      static_cast<unsigned long>(stats->getNodesCreated()));
            }
            matchManager->clearPendingRecords();
            transitionToSleepState = true;
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "upload: request failed code=%d message=%s (retry %d/3)",
                  static_cast<int>(error.code), error.message.c_str(), retryCount);
            if (retryCount < 3) {
                retryCount++;
                shouldRetryUpload = true;
                uploadTimer.setTimer(UPLOAD_TIMEOUT_MS);
            } else {
                LOG_E(TAG, "upload: out of retries, going to sleep with records still pending");
                transitionToSleepState = true;
            }
        });
}

void NodeUploadMatches::onStateMounted(PDN* pdn) {
    pdn->getWirelessManager()->enableWifiMode();
    retryCount = 0;
    transitionToSleepState = false;
    shouldRetryUpload = false;
    uploadTimer.setTimer(UPLOAD_TIMEOUT_MS);
    const uint8_t* peerMac = matchManager->getTrackedPeerMac();
    if (peerMac != nullptr) {
        memcpy(uploadPeerMac, peerMac, sizeof(uploadPeerMac));
    }
    LOG_I(TAG, "upload: mounted, partner=%s, timeout=%d ms",
          peerMac != nullptr ? MacToString(peerMac) : "none", UPLOAD_TIMEOUT_MS);
    attemptUpload(pdn);
}

void NodeUploadMatches::onStateLoop(PDN* pdn) {
    uploadTimer.updateTime();
    if (shouldRetryUpload && wirelessManager->isWifiConnected()) {
        LOG_I(TAG, "upload: wifi connected, retrying");
        shouldRetryUpload = false;
        attemptUpload(pdn);
    }
    if (uploadTimer.expired() && !transitionToSleepState) {
        LOG_W(TAG, "upload: timed out (wifiConnected=%d, requestsQueued=%d), going to sleep",
              wirelessManager->isWifiConnected() ? 1 : 0,
              wirelessManager->hasQueuedHttpRequests() ? 1 : 0);
        transitionToSleepState = true;
    }
}

void NodeUploadMatches::onStateDismounted(PDN* pdn) {
    uploadTimer.invalidate();
    transitionToSleepState = false;
    shouldRetryUpload = false;
    pdn->getLightManager()->stopAnimation();
}

bool NodeUploadMatches::transitionToSleep() {
    return transitionToSleepState;
}

// --- NodeSleep ---

NodeSleep::NodeSleep(MatchManagerV2* matchManager)
    : TypedState<PDN>(NODE_SLEEP), matchManager(matchManager) {}

NodeSleep::~NodeSleep() {
    matchManager = nullptr;
}

void NodeSleep::onStateMounted(PDN* pdn) {
    matchManager->clearFailedAttemptsForCurrentPeer();
    transitionToAwakenState = false;
    pdn->getDisplay()
        ->invalidateScreen()
        ->drawImage(nodeNetworkImage(ImageType::LOGO_LEFT))
        ->drawImage(nodeNetworkImage(ImageType::LOGO_RIGHT))
        ->render();
    dormantTimer.setTimer(SLEEP_DURATION_MS);
}

void NodeSleep::onStateLoop(PDN* pdn) {
    dormantTimer.updateTime();
    if (dormantTimer.expired()) {
        transitionToAwakenState = true;
    }
}

void NodeSleep::onStateDismounted(PDN* pdn) {
    dormantTimer.invalidate();
}

bool NodeSleep::transitionToAwaken() {
    return transitionToAwakenState;
}

// --- NodeAwaken ---

NodeAwaken::NodeAwaken() : TypedState<PDN>(NODE_AWAKEN) {}

NodeAwaken::~NodeAwaken() = default;

void NodeAwaken::onStateMounted(PDN* pdn) {
    activateMotorCount = 0;
    activateMotor = true;
    activationSequenceTimer.setTimer(ACTIVATION_STEP_MS);
    pdn->getHaptics()->setIntensity(125);
    pdn->getDisplay()
        ->invalidateScreen()
        ->drawImage(nodeNetworkImage(ImageType::LOGO_LEFT))
        ->drawImage(nodeNetworkImage(ImageType::STAMP))
        ->render();
}

void NodeAwaken::onStateLoop(PDN* pdn) {
    if (!activationSequenceTimer.expired()) {
        return;
    }
    if (activateMotorCount <= AWAKEN_THRESHOLD) {
        pdn->getHaptics()->setIntensity(activateMotor ? VIBRATION_MAX : VIBRATION_OFF);
        activationSequenceTimer.setTimer(ACTIVATION_STEP_MS);
        activateMotorCount++;
        activateMotor = !activateMotor;
    } else {
        pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    }
}

void NodeAwaken::onStateDismounted(PDN* pdn) {
    activationSequenceTimer.invalidate();
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    activateMotorCount = 0;
    activateMotor = false;
}

bool NodeAwaken::transitionToIdle() {
    return activateMotorCount > AWAKEN_THRESHOLD;
}
