#include "apps/quickdraw-demo/quickdraw-practice.hpp"

namespace {
constexpr uint8_t kFdnDefaultBrightness = 255;

void applyFdnPracticeBrightness(FDN* fdn) {
    fdn->getLightManager()->setGlobalBrightness(QuickdrawCountdown::kFdnPracticeBrightness);
}

void restoreFdnDefaultBrightness(FDN* fdn) {
    fdn->getLightManager()->setGlobalBrightness(kFdnDefaultBrightness);
}
}  // namespace

QuickdrawPracticeSession::QuickdrawPracticeSession(ControllerWirelessManager* controllerWirelessManager)
    : controllerWirelessManager_(controllerWirelessManager) {}

void QuickdrawPracticeSession::setOpponentResponseMs(unsigned long opponentResponseMs) {
    opponentResponseMs_ = opponentResponseMs;
}

void QuickdrawPracticeSession::setOpponentEnabled(bool enabled) {
    opponentEnabled_ = enabled;
}

void QuickdrawPracticeSession::resetMatch() {
    phase_ = QuickdrawPracticePhase::MESSAGE;
    outcome_ = QuickdrawPracticeOutcome::NONE;
    countdownIndex_ = 0;
    drawReached_ = false;
    playerShot_ = false;
    drawStartMs_ = 0;
    playerReactionMs_ = 0;
    readyTimer_.invalidate();
    countdownTimer_.invalidate();
    duelTimer_.invalidate();
    outcomeTimer_.invalidate();
    hapticTimer_.invalidate();
}

void QuickdrawPracticeSession::beginReady() {
    phase_ = QuickdrawPracticePhase::READY;
    outcome_ = QuickdrawPracticeOutcome::NONE;
    countdownIndex_ = 0;
    drawReached_ = false;
    playerShot_ = false;
    drawStartMs_ = 0;
    playerReactionMs_ = 0;
    countdownTimer_.invalidate();
    duelTimer_.invalidate();
    readyTimer_.setTimer(QuickdrawCountdown::kReadyDurationMs);
}

void QuickdrawPracticeSession::beginCountdown() {
    phase_ = QuickdrawPracticePhase::COUNTDOWN;
    countdownIndex_ = 0;
    drawReached_ = false;
    playerShot_ = false;
    drawStartMs_ = 0;
    playerReactionMs_ = 0;
    readyTimer_.invalidate();
    duelTimer_.invalidate();

    const QuickdrawCountdown::Stage& stage = QuickdrawCountdown::queue()[countdownIndex_];
    countdownTimer_.setTimer(stage.durationMs);
}

void QuickdrawPracticeSession::clearOutcome() {
    outcome_ = QuickdrawPracticeOutcome::NONE;
    outcomeTimer_.invalidate();
}

void QuickdrawPracticeSession::update(FDN* fdn) {
    updateHaptic(fdn);

    if (phase_ == QuickdrawPracticePhase::READY) {
        readyTimer_.updateTime();
        if (readyTimer_.expired()) {
            beginCountdown();
            syncCountdownStep(fdn, QuickdrawCountdown::Step::THREE);
        }
        return;
    }

    if (phase_ == QuickdrawPracticePhase::COUNTDOWN) {
        countdownTimer_.updateTime();
        if (countdownTimer_.expired()) {
            advanceCountdown(fdn);
        }
        return;
    }

    if (phase_ == QuickdrawPracticePhase::DRAW) {
        duelTimer_.updateTime();

        if (!playerShot_) {
            const unsigned long elapsed = SimpleTimer::getPlatformClock()->milliseconds() - drawStartMs_;
            if (opponentEnabled_ && elapsed >= opponentResponseMs_) {
                outcome_ = QuickdrawPracticeOutcome::LOSE;
                phase_ = QuickdrawPracticePhase::OUTCOME;
                duelTimer_.invalidate();
            } else if (duelTimer_.expired()) {
                outcome_ = QuickdrawPracticeOutcome::TIMEOUT;
                phase_ = QuickdrawPracticePhase::OUTCOME;
            }
        }
        return;
    }

    if (phase_ == QuickdrawPracticePhase::OUTCOME) {
        outcomeTimer_.updateTime();
    }
}

void QuickdrawPracticeSession::renderTutorialMessage(
    FDN* fdn, const char* line1, const char* line2, const char* line3) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    display->drawCenteredText(line1, 18);
    display->drawCenteredText(line2, 34);
    display->drawCenteredText(line3, 50);
    display->render();
}

void QuickdrawPracticeSession::renderReady(FDN* fdn, bool showTimeToBeat) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);

    if (showTimeToBeat) {
        char beatLine[24];
        snprintf(beatLine, sizeof(beatLine), "BEAT: %lums", opponentResponseMs_);
        display->drawCenteredText("Ready?", 22);
        display->drawCenteredText(beatLine, 42);
    } else {
        display->drawCenteredText("Ready?", 32);
    }

    display->render();
}

void QuickdrawPracticeSession::renderStepImage(FDN* fdn, QuickdrawCountdown::Step step) {
    const Image image = quickdrawImageFor(QuickdrawCountdown::imageTypeForStep(step));
    fdn->getDisplay()->invalidateScreen()->drawImage(image)->render();
}

void QuickdrawPracticeSession::renderOutcomeImage(FDN* fdn, ImageType imageType) {
    const Image image = quickdrawImageFor(imageType);
    fdn->getDisplay()->invalidateScreen()->drawImage(image)->render();
}

void QuickdrawPracticeSession::renderCountdown(FDN* fdn) {
    if (countdownIndex_ >= QuickdrawCountdown::queueLength()) {
        return;
    }

    const QuickdrawCountdown::Step step = QuickdrawCountdown::queue()[countdownIndex_].step;
    renderStepImage(fdn, step);
}

void QuickdrawPracticeSession::renderDraw(FDN* fdn) {
    renderStepImage(fdn, QuickdrawCountdown::Step::DRAW);
}

void QuickdrawPracticeSession::renderRoundResult(
    FDN* fdn, QuickdrawPracticeOutcome outcome, unsigned long reactionMs) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);

    if (outcome == QuickdrawPracticeOutcome::EARLY_SHOT) {
        display->drawCenteredText("TOO EARLY!", 32);
    } else if (outcome == QuickdrawPracticeOutcome::TIMEOUT ||
               (outcome == QuickdrawPracticeOutcome::LOSE && reactionMs == 0)) {
        display->drawCenteredText("TOO LATE!", 32);
    } else if (reactionMs > 0) {
        char timeLine[16];
        snprintf(timeLine, sizeof(timeLine), "%lums", reactionMs);
        display->drawCenteredText("YOUR TIME:", 22);
        display->drawCenteredText(timeLine, 42);
    } else {
        display->drawCenteredText("TOO LATE!", 32);
    }

    display->render();
}

void QuickdrawPracticeSession::onPlayerShot(bool drawReached) {
    if (playerShot_ || phase_ == QuickdrawPracticePhase::OUTCOME) {
        return;
    }

    playerShot_ = true;

    if (phase_ == QuickdrawPracticePhase::COUNTDOWN || !drawReached) {
        outcome_ = QuickdrawPracticeOutcome::EARLY_SHOT;
        phase_ = QuickdrawPracticePhase::OUTCOME;
        countdownTimer_.invalidate();
        duelTimer_.invalidate();
        return;
    }

    if (phase_ == QuickdrawPracticePhase::DRAW) {
        const unsigned long reactionMs =
            SimpleTimer::getPlatformClock()->milliseconds() - drawStartMs_;
        playerReactionMs_ = reactionMs;
        resolveDraw(reactionMs);
    }
}

bool QuickdrawPracticeSession::readyTimerExpired() {
    return readyTimer_.isRunning() && readyTimer_.expired();
}

bool QuickdrawPracticeSession::outcomeTimerExpired() {
    return outcomeTimer_.isRunning() && outcomeTimer_.expired();
}

bool QuickdrawPracticeSession::matchResolved() const {
    return phase_ == QuickdrawPracticePhase::OUTCOME && outcome_ != QuickdrawPracticeOutcome::NONE;
}

void QuickdrawPracticeSession::showWinAnimation(FDN* fdn) {
    renderOutcomeImage(fdn, ImageType::WIN);
    syncAnimation(fdn, QuickdrawCountdown::PeripheralAnimationId::HUNTER_WIN);
    applyFdnPracticeBrightness(fdn);
    AnimationConfig config;
    config.loop = true;
    config.speed = 16;
    config.loopDelayMs = 0;
    config.initialState = LEDState();
    fdn->getLightManager()->startAnimation(new FDNMatchSuccessAnimation(), config);
    outcomeTimer_.setTimer(QuickdrawCountdown::kOutcomeDurationMs);
}

void QuickdrawPracticeSession::showLoseAnimation(FDN* fdn) {
    renderOutcomeImage(fdn, ImageType::LOSE);
    syncAnimation(fdn, QuickdrawCountdown::PeripheralAnimationId::LOSE);
    applyFdnPracticeBrightness(fdn);
    AnimationConfig config;
    config.loop = true;
    config.speed = 16;
    config.loopDelayMs = 0;
    config.initialState = LEDState();
    fdn->getLightManager()->startAnimation(new FDNLoseAnimation(), config);
    outcomeTimer_.setTimer(QuickdrawCountdown::kOutcomeDurationMs);
}

void QuickdrawPracticeSession::clearAnimations(FDN* fdn) {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_LEDS, 0, 0);
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_DISPLAY, 0, 0);
    fdn->getLightManager()->stopAnimation();
    fdn->getLightManager()->clear();
    restoreFdnDefaultBrightness(fdn);
}

void QuickdrawPracticeSession::advanceCountdown(FDN* fdn) {
    countdownIndex_++;

    if (countdownIndex_ >= QuickdrawCountdown::queueLength()) {
        enterDraw(fdn);
        return;
    }

    const QuickdrawCountdown::Stage& stage = QuickdrawCountdown::queue()[countdownIndex_];
    if (stage.step == QuickdrawCountdown::Step::DRAW) {
        enterDraw(fdn);
        return;
    }

    syncCountdownStep(fdn, stage.step);
    countdownTimer_.setTimer(stage.durationMs);
}

void QuickdrawPracticeSession::enterDraw(FDN* fdn) {
    phase_ = QuickdrawPracticePhase::DRAW;
    drawReached_ = true;
    drawStartMs_ = SimpleTimer::getPlatformClock()->milliseconds();
    countdownTimer_.invalidate();
    duelTimer_.setTimer(QuickdrawCountdown::kDuelTimeoutMs);
    syncCountdownStep(fdn, QuickdrawCountdown::Step::DRAW);
    renderDraw(fdn);
    pulseHaptic(fdn);
}

void QuickdrawPracticeSession::resolveDraw(unsigned long reactionMs) {
    if (!opponentEnabled_ || reactionMs < opponentResponseMs_) {
        outcome_ = QuickdrawPracticeOutcome::WIN;
    } else {
        outcome_ = QuickdrawPracticeOutcome::LOSE;
    }

    phase_ = QuickdrawPracticePhase::OUTCOME;
    duelTimer_.invalidate();
}

void QuickdrawPracticeSession::syncCountdownStep(FDN* fdn, QuickdrawCountdown::Step step) {
    syncAnimation(
        fdn,
        QuickdrawCountdown::PeripheralAnimationId::COUNTDOWN,
        static_cast<uint8_t>(step));
    pulseHaptic(fdn);

    applyFdnPracticeBrightness(fdn);
    AnimationConfig config = QuickdrawCountdown::fdnAnimationConfigFor(step);
    fdn->getLightManager()->startAnimation(new FdnCountdownAnimation(), config);
    renderStepImage(fdn, step);

    for (size_t i = 0; i < QuickdrawCountdown::queueLength(); i++) {
        if (QuickdrawCountdown::queue()[i].step == step) {
            countdownIndex_ = i;
            break;
        }
    }
}

void QuickdrawPracticeSession::syncAnimation(
    FDN* fdn,
    QuickdrawCountdown::PeripheralAnimationId animationId,
    uint8_t param2) {
    (void)fdn;
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::DISPLAY_ANIMATION,
        static_cast<uint8_t>(animationId),
        param2);
}

void QuickdrawPracticeSession::pulsePlayerAck() {
    // Quick 100 ms LED + haptic pulse on the connected PDN.
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_HAPTIC, 0, 1);
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_LED, 0, 1);
}

void QuickdrawPracticeSession::pulseHaptic(FDN* fdn) {
    (void)fdn;
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_HAPTIC,
        0,
        1);
    hapticTimer_.setTimer(QuickdrawCountdown::kHapticDurationMs);
}

void QuickdrawPracticeSession::updateHaptic(FDN* fdn) {
    (void)fdn;

    if (!hapticTimer_.isRunning()) {
        return;
    }

    hapticTimer_.updateTime();
    if (hapticTimer_.expired()) {
        hapticTimer_.invalidate();
    }
}
