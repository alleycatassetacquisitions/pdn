#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#include "game/node-network/node-network-states.hpp"

#include "alleycat-server/matches-api.hpp"
#include "device/animation/blink-animation.hpp"
#include "device/animation/countdown-animation.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/step-drain-animation.hpp"
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

NodeDuelCountdown::CountdownStep NodeDuelCountdown::stepFor(int32_t msUntilStart) {
    constexpr int32_t STAGE_MS = static_cast<int32_t>(NODE_COUNTDOWN_MS / 3);
    if (msUntilStart > 2 * STAGE_MS) {
        return CountdownStep::THREE;
    }
    if (msUntilStart > STAGE_MS) {
        return CountdownStep::TWO;
    }
    return msUntilStart > 0 ? CountdownStep::ONE : CountdownStep::BATTLE;
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

void NodeDuelCountdown::showStep(PDN* pdn, CountdownStep step) {
    shownStep = step;
    pdn->getDisplay()->invalidateScreen()->drawImage(nodeNetworkImage(imageFor(step)))->render();
    // Each step restarts the animation with its own initial state, as Quickdraw's countdown does.
    pdn->getLightManager()->startAnimation(new CountdownAnimation(), animationConfigFor(step));
    pdn->getHaptics()->setIntensity(HAPTIC_INTENSITY);
    hapticTimer.setTimer(HAPTIC_DURATION);
}

void NodeDuelCountdown::onStateMounted(PDN* pdn) {
    doBattle = false;
    probesSent = 0;
    phaseTimer.invalidate();
    if (matchManager->getLocalLinkSide() == DuelLinkSide::INITIATOR) {
        phase = Phase::SYNCING;
    } else {
        phase = Phase::AWAITING_SEQUENCE;
        phaseTimer.setTimer(NODE_SEQUENCE_WAIT_MS);
    }
    showStep(pdn, CountdownStep::THREE);
}

void NodeDuelCountdown::loopSyncing() {
    if (probesSent < NODE_SYNC_PROBE_COUNT) {
        if (probesSent == 0 || phaseTimer.expired()) {
            matchManager->sendSyncProbe();
            probesSent++;
            phaseTimer.setTimer(probesSent < NODE_SYNC_PROBE_COUNT ? NODE_SYNC_PROBE_INTERVAL_MS
                                                                   : NODE_SYNC_SETTLE_MS);
        }
        return;
    }
    if (!phaseTimer.expired()) {
        return;
    }
    if (!matchManager->hasClockSync()) {
        LOG_W(TAG, "no clock sync within %lu ms round trip",
              static_cast<unsigned long>(NODE_SYNC_MAX_RTT_MS));
        matchManager->abortRound();
        return;
    }
    static std::mt19937 rng(std::random_device{}());
    const NodeInputSequence sequence =
    generateNodeInputSequence([]() { return static_cast<uint32_t>(rng()); });
    if (!matchManager->beginRound(sequence)) {
        LOG_E(TAG, "could not start the round");
        matchManager->abortRound();
        return;
    }
    phase = Phase::COUNTING;
}

void NodeDuelCountdown::loopCounting(PDN* pdn) {
    const int32_t msUntilStart = matchManager->msUntilStep(0);
    const DuelSession& session = matchManager->getSession();
    const bool isInitiator = session.getLocalSide() == DuelLinkSide::INITIATOR;
    if (isInitiator && session.isSequenceUndelivered()) {
        matchManager->abortRound();
        return;
    }
    const CountdownStep step = stepFor(msUntilStart);
    if (step != CountdownStep::BATTLE) {
        if (step != shownStep) {
            showStep(pdn, step);
        }
        return;
    }
    if (isInitiator && !session.isSequenceDelivered()) {
        LOG_W(TAG, "SEQUENCE not acknowledged by step 0");
        matchManager->abortRound();
        return;
    }
    doBattle = true;
}

void NodeDuelCountdown::onStateLoop(PDN* pdn) {
    if (hapticTimer.expired()) {
        pdn->getHaptics()->setIntensity(0);
        hapticTimer.invalidate();
    }
    if (doBattle || matchManager->isAborted()) {
        return;
    }
    if (!matchManager->getSession().isActive()) {
        LOG_W(TAG, "session dropped during countdown");
        matchManager->abortRound();
        return;
    }
    switch (phase) {
        case Phase::SYNCING: loopSyncing(); return;
        case Phase::AWAITING_SEQUENCE:
            if (matchManager->hasSequence()) {
                phase = Phase::COUNTING;
            } else if (phaseTimer.expired()) {
                LOG_W(TAG, "no SEQUENCE within %lu ms", NODE_SEQUENCE_WAIT_MS);
                matchManager->abortRound();
            }
            return;
        case Phase::COUNTING: loopCounting(pdn); return;
    }
}

void NodeDuelCountdown::onStateDismounted(PDN* pdn) {
    if (!doBattle && !matchManager->isAborted()) {
        matchManager->clearCurrentMatch();
    }
    doBattle = false;
    probesSent = 0;
    phaseTimer.invalidate();
    hapticTimer.invalidate();
    pdn->getLightManager()->stopAnimation();
    pdn->getHaptics()->setIntensity(0);
}

bool NodeDuelCountdown::shallWeBattle() {
    return doBattle;
}

bool NodeDuelCountdown::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool NodeDuelCountdown::abortedToError() { return matchManager->isAborted(); }

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

void NodeDuel::onButtonPressed(ButtonIdentifier button) {
    const DuelSession& session = matchManager->getSession();
    if (!stepShown || stepDisplayEnded || localPressJudged ||
        !session.isLocalPresser(currentStep) ||
        matchManager->msUntilWindowCloses(currentStep) <= 0) {
        return;
    }
    const NodePrompt& prompt = session.getPrompt(currentStep, session.getLocalSide());
    const bool correct = button == prompt.buttons[localPressCount];
    localPressCount++;
    LOG_I(TAG, "step %u press %u/%u: %s, %s", currentStep, localPressCount, prompt.count,
          button == ButtonIdentifier::PRIMARY_BUTTON ? "primary" : "secondary",
          correct ? "correct" : "wrong");
    if (correct && localPressCount < prompt.count) {
        matchManager->onLocalPress(currentStep, localPressCount);
        return;
    }
    localPressJudged = true;
    matchManager->onLocalInputResult(currentStep, correct, localPressCount);
}

bool NodeDuel::showsNow() const {
    const DuelSession& session = matchManager->getSession();
    return session.isLocalPresser(currentStep) && !session.isOpponentPresser(currentStep);
}

uint8_t NodeDuel::pressesToShow() const {
    const DuelSession& session = matchManager->getSession();
    return session.isLocalPresser(currentStep) ? localPressCount
                                               : session.getOpponentPresses(currentStep);
}

void NodeDuel::showStep(PDN* pdn) {
    const DuelSession& session = matchManager->getSession();
    stepShown = true;
    resultScreenPending = false;
    drawStep(pdn);
    const unsigned long windowLeftMs =
    static_cast<unsigned long>(matchManager->msUntilWindowCloses(currentStep));
    AnimationConfig config;
    config.speed = 16;
    config.loop = false;
    pdn->getLightManager()->startAnimation(
    new StepDrainAnimation(nodeNetworkStepDrainColors, NODE_DUEL_LED_BRIGHTNESS, windowLeftMs),
    config);
    LOG_I(TAG, "step %u: local %s", currentStep,
          showsNow()                               ? "sees NOW"
          : session.isLocalPresser(currentStep) ? "presses with the opponent"
                                                  : "views");
}

void NodeDuel::drawStep(PDN* pdn) {
    const DuelSession& session = matchManager->getSession();
    Display* display = pdn->getDisplay()->invalidateScreen();
    drawnPressCount = pressesToShow();
    if (showsNow()) {
        display->drawImage(nodeNetworkImage(ImageType::IDLE))
        ->drawImage(nodeNetworkImage(ImageType::DRAW));
    } else {
        const DuelLinkSide shownSide =
        session.isLocalPresser(currentStep) ? session.getLocalSide() : session.getOpponentSide();
        const NodePrompt& prompt = session.getPrompt(currentStep, shownSide);
        for (uint8_t press = 0; press < prompt.count; press++) {
            const Image image =
            nodeNetworkImage(prompt.buttons[press] == ButtonIdentifier::PRIMARY_BUTTON
                             ? ImageType::PRIMARY_BUTTON
                             : ImageType::SECONDARY_BUTTON);
            const int x = prompt.count == 1 ? -1 : press * SECOND_PROMPT_X;
            if (press < drawnPressCount) {
                display->drawImageInverted(image, x, -1);
            } else {
                display->drawImage(image, x, -1);
            }
        }
    }
    display->render();
}

void NodeDuel::endStepDisplay(PDN* pdn) {
    if (!stepShown || stepDisplayEnded) {
        return;
    }
    stepDisplayEnded = true;
    drawStepResult(pdn, currentStep);
    pdn->getHaptics()->setIntensity(HAPTIC_INTENSITY);
    hapticTimer.setTimer(HAPTIC_DURATION);
}

void NodeDuel::drawStepResult(PDN* pdn, uint8_t step) {
    const NodeStepResult result = matchManager->getSession().getStepResult(step);
    const std::string stageText =
    std::to_string(step + 1) + "/" + std::to_string(NODE_SEQUENCE_LENGTH);
    Display* display = pdn->getDisplay()
                       ->invalidateScreen()
                       ->drawImage(nodeNetworkImage(ImageType::IDLE))
                       ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
                       ->drawText("Stage", 70, 20)
                       ->drawText(stageText.c_str(), 70, 35);
    if (result != NodeStepResult::PENDING) {
        display->drawText(result == NodeStepResult::SUCCESS ? "PASS" : "MISS", 70, 50);
    }
    display->render();
    if (result == NodeStepResult::PENDING) {
        pdn->getLightManager()->stopAnimation();
    } else {
        AnimationConfig config;
        config.speed = 16;
        config.loop = false;
        pdn->getLightManager()->startAnimation(
        new BlinkAnimation(result == NodeStepResult::SUCCESS ? nodeNetworkPassColor
                                                             : nodeNetworkMissColor,
                           NODE_DUEL_LED_BRIGHTNESS, NODE_RESULT_BLINK_ON_MS,
                           NODE_RESULT_BLINK_OFF_MS, NODE_RESULT_BLINK_COUNT),
        config);
    }
    resultScreenStep = step;
    resultScreenPending = result == NodeStepResult::PENDING;
}

void NodeDuel::refreshStepResult(PDN* pdn) {
    if (resultScreenPending && matchManager->getSession().hasStepResult(resultScreenStep)) {
        drawStepResult(pdn, resultScreenStep);
    }
}

void NodeDuel::concludeRound() {
    if (roundConcluded) {
        return;
    }
    matchManager->concludeRound();
    roundConcluded = true;
    LOG_I(TAG, "round over: score=%u success=%d retries left=%lu",
          matchManager->getLastRoundScore(), matchManager->wasLastRoundSuccess() ? 1 : 0,
          static_cast<unsigned long>(matchManager->getRetriesRemaining()));
}

void NodeDuel::runSchedule(PDN* pdn) {
    const DuelSession& session = matchManager->getSession();
    if (matchManager->msUntilStep(currentStep) > 0) {
        return;
    }
    if (matchManager->msUntilWindowCloses(currentStep) > 0) {
        if (!stepShown) {
            showStep(pdn);
        } else if (stepDisplayEnded) {
            return;
        } else if (session.hasStepResult(currentStep)) {
            endStepDisplay(pdn);
        } else if (!showsNow() && pressesToShow() != drawnPressCount) {
            drawStep(pdn);
        }
        return;
    }
    if (session.isLocalPresser(currentStep) && !localPressJudged) {
        localPressJudged = true;
        matchManager->onLocalInputResult(currentStep, false, localPressCount);
        LOG_I(TAG, "step %u: window closed after %u presses", currentStep, localPressCount);
    }
    endStepDisplay(pdn);
    currentStep++;
    stepShown = false;
    stepDisplayEnded = false;
    localPressJudged = false;
    localPressCount = 0;
    drawnPressCount = 0;
}

void NodeDuel::finishAsInitiator() {
    const DuelSession& session = matchManager->getSession();
    if (!session.isOutcomeKnown()) {
        const int32_t msSinceLastWindow =
        -matchManager->msUntilWindowCloses(NODE_SEQUENCE_LENGTH - 1);
        if (!session.allStepsResolved() &&
            msSinceLastWindow < static_cast<int32_t>(NODE_RESULT_GRACE_MS)) {
            return;
        }
        matchManager->finishRound();
        return;
    }
    if (session.isOutcomeDelivered()) {
        concludeRound();
    } else if (session.isOutcomeUndelivered()) {
        LOG_W(TAG, "OUTCOME never acknowledged");
        matchManager->abortRound();
    }
}

void NodeDuel::finishAsResponder() {
    const DuelSession& session = matchManager->getSession();
    if (session.isOutcomeKnown()) {
        concludeRound();
        return;
    }
    const int32_t msSinceLastWindow = -matchManager->msUntilWindowCloses(NODE_SEQUENCE_LENGTH - 1);
    if (msSinceLastWindow < static_cast<int32_t>(NODE_OUTCOME_WAIT_MS)) {
        return;
    }
    LOG_W(TAG, "no OUTCOME from the initiator");
    matchManager->abortRound();
}

void NodeDuel::onStateMounted(PDN* pdn) {
    currentStep = 0;
    stepShown = false;
    stepDisplayEnded = false;
    localPressJudged = false;
    localPressCount = 0;
    drawnPressCount = 0;
    roundConcluded = false;
    resultScreenPending = false;

    parameterizedCallbackFunction primaryPress = [](void* ctx) {
        static_cast<NodeDuel*>(ctx)->onButtonPressed(ButtonIdentifier::PRIMARY_BUTTON);
    };
    parameterizedCallbackFunction secondaryPress = [](void* ctx) {
        static_cast<NodeDuel*>(ctx)->onButtonPressed(ButtonIdentifier::SECONDARY_BUTTON);
    };
    pdn->getPrimaryButton()->setButtonPress(primaryPress, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(secondaryPress, this, ButtonInteraction::CLICK);

    pdn->getDisplay()->invalidateScreen()->drawImage(nodeNetworkImage(ImageType::IDLE))->render();
    pdn->getLightManager()->stopAnimation();
}

void NodeDuel::onStateLoop(PDN* pdn) {
    if (hapticTimer.expired()) {
        pdn->getHaptics()->setIntensity(0);
        hapticTimer.invalidate();
    }
    if (roundConcluded || matchManager->isAborted()) {
        return;
    }
    if (!matchManager->getSession().isActive()) {
        LOG_W(TAG, "session dropped mid-round");
        matchManager->abortRound();
        return;
    }
    refreshStepResult(pdn);
    if (currentStep < NODE_SEQUENCE_LENGTH) {
        runSchedule(pdn);
    } else if (matchManager->getLocalLinkSide() == DuelLinkSide::INITIATOR) {
        finishAsInitiator();
    } else {
        finishAsResponder();
    }
}

void NodeDuel::onStateDismounted(PDN* pdn) {
    if (!roundConcluded && !matchManager->isAborted()) {
        matchManager->clearCurrentMatch();
    }
    hapticTimer.invalidate();
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    pdn->getLightManager()->stopAnimation();
    pdn->getHaptics()->setIntensity(0);
    roundConcluded = false;
}

bool NodeDuel::transitionToScore() { return roundConcluded; }

bool NodeDuel::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool NodeDuel::abortedToError() { return matchManager->isAborted(); }

bool NodeDuel::isPrimaryRequired() {
    return matchManager->getLocalLinkSide() == DuelLinkSide::INITIATOR;
}

bool NodeDuel::isAuxRequired() {
    return matchManager->getLocalLinkSide() == DuelLinkSide::RESPONDER;
}

// --- NodeScore ---

NodeScore::NodeScore(MatchManagerV2* matchManager)
    : TypedState<PDN>(NODE_SCORE)
    , matchManager(matchManager) {}

NodeScore::~NodeScore() { matchManager = nullptr; }

void NodeScore::onStateMounted(PDN* pdn) {
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    pdn->getDisplay()->invalidateScreen()->drawImage(nodeNetworkImage(ImageType::IDLE))->render();
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Score", 70, 20);
    const std::string scoreText =
    std::to_string(matchManager->getLastRoundScore()) + "/" + std::to_string(NODE_SEQUENCE_LENGTH);
    pdn->getDisplay()
    ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
    ->drawText(scoreText.c_str(), 72, 50);
    pdn->getDisplay()->render();
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void NodeScore::onStateLoop(PDN* pdn) { displayTimer.updateTime(); }

void NodeScore::onStateDismounted(PDN* pdn) { displayTimer.invalidate(); }

bool NodeScore::transitionToWin() {
    return displayTimer.expired() && matchManager->wasLastRoundSuccess();
}

bool NodeScore::transitionToLinkRetry() {
    return displayTimer.expired() && !matchManager->wasLastRoundSuccess();
}

// --- NodeError ---

NodeError::NodeError(MatchManagerV2* matchManager)
    : TypedState<PDN>(NODE_ERROR)
    , matchManager(matchManager) {}

NodeError::~NodeError() { matchManager = nullptr; }

void NodeError::onStateMounted(PDN* pdn) {
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    pdn->getDisplay()->invalidateScreen()->drawImage(nodeNetworkImage(ImageType::IDLE))->render();
    pdn->getDisplay()
    ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
    ->drawText("Error", 70, 20)
    ->drawText("Retrying", 70, 35);
    pdn->getDisplay()->render();
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void NodeError::onStateLoop(PDN* pdn) { displayTimer.updateTime(); }

void NodeError::onStateDismounted(PDN* pdn) {
    displayTimer.invalidate();
    matchManager->clearAbort();
}

bool NodeError::transitionToIdle() { return displayTimer.expired(); }

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
