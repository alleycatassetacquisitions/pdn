#include "apps/bonk-it/bonk-it-session.hpp"
#include "device/animation/fdn-bonk-fade-animation.hpp"
#include "device/animation/fdn-lose-animation.hpp"
#include "game/bonk-it-peripheral.hpp"
#include <algorithm>
#include <cstdlib>

namespace {
constexpr unsigned long kLossPulseDurationMs  = 300;
constexpr unsigned long kLossPulseGapMs     = 150;
constexpr unsigned long kLoseAnimationMs    = 3000;
constexpr int           kTimerBarSegments   = 16;
constexpr int           kTimerBarY          = 4;
constexpr int           kCueGlyphY          = 46;
}  // namespace

BonkItSession::BonkItSession(ControllerWirelessManager* controllerWirelessManager)
    : controllerWirelessManager_(controllerWirelessManager) {}

void BonkItSession::reset() {
    roundActive_          = false;
    activeCue_            = BonkCue::UP;
    result_               = BonkRoundResult::NONE;
    roundDurationMs_      = 0;
    showTimerBar_         = true;
    roundTimer_.invalidate();
    lossPulseActive_      = false;
    lossPulsesSent_       = 0;
    lossPulseOn_          = false;
    lossPulseTimer_.invalidate();
    showingLoseAnimation_ = false;
    loseAnimationTimer_.invalidate();
}

const char* BonkItSession::glyphForCue(BonkCue cue) {
    switch (cue) {
        case BonkCue::UP:
            return BonkItGlyphs::upArrow();
        case BonkCue::DOWN:
            return BonkItGlyphs::downArrow();
        case BonkCue::X:
            return BonkItGlyphs::xMark();
    }
    return BonkItGlyphs::upArrow();
}

unsigned long BonkItSession::durationForRound(int roundIndex) {
    if (roundIndex < 0) {
        roundIndex = 0;
    }

    constexpr unsigned long kStartMs = 2000;
    constexpr unsigned long kMinMs   = 200;

    unsigned long duration = kStartMs;
    for (int i = 0; i < roundIndex; ++i) {
        unsigned long step;
        if (duration > 1400) {
            step = 40;
        } else if (duration > 700) {
            step = 20;
        } else if (duration > 400) {
            step = 10;
        } else {
            step = 5;
        }

        duration = (duration > step + kMinMs) ? duration - step : kMinMs;
    }

    return duration;
}

int BonkItSession::fadeSpeedForRound(int roundIndex) {
    constexpr int kFastestSpeed = 8;
    constexpr int kSlowestSpeed = 32;
    constexpr unsigned long kFastestDuration = 200;
    constexpr unsigned long kSlowestDuration = 2000;

    const unsigned long duration = durationForRound(roundIndex);
    if (duration >= kSlowestDuration) {
        return kSlowestSpeed;
    }
    if (duration <= kFastestDuration) {
        return kFastestSpeed;
    }

    return kFastestSpeed + static_cast<int>(
        (duration - kFastestDuration) * (kSlowestSpeed - kFastestSpeed) /
        (kSlowestDuration - kFastestDuration));
}

BonkCue BonkItSession::randomCue() {
    return static_cast<BonkCue>(std::rand() % 3);
}

void BonkItSession::renderTutorialMessage(FDN* fdn, const char* line1) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    display->drawCenteredText(line1, 32);
    display->render();
}

void BonkItSession::renderTutorialMessage(FDN* fdn, const char* line1, const char* line2) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    display->drawCenteredText(line1, 24);
    display->drawCenteredText(line2, 40);
    display->render();
}

void BonkItSession::renderTutorialMessage(
    FDN* fdn, const char* line1, const char* line2, const char* line3) {
    Display* display = fdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    display->drawCenteredText(line1, 18);
    display->drawCenteredText(line2, 34);
    display->drawCenteredText(line3, 50);
    display->render();
}

void BonkItSession::renderFeedbackMessage(FDN* fdn, const char* line) {
    renderTutorialMessage(fdn, line);
}

void BonkItSession::renderReadyMessage(FDN* fdn) {
    renderTutorialMessage(fdn, "Ready?");
}

void BonkItSession::renderCueGlyph(FDN* fdn, BonkCue cue) {
    Display* display = fdn->getDisplay();
    const char* glyph = glyphForCue(cue);
    display->invalidateScreen();
    display->setGlyphMode(FontMode::SYMBOL_GLYPH);
    const int glyphWidth = display->getTextWidth(glyph);
    const int x = (display->getWidth() - glyphWidth) / 2;
    display->renderGlyph(glyph, x, kCueGlyphY);
}

void BonkItSession::renderTimerBar(FDN* fdn, float remainingFraction) {
    Display* display = fdn->getDisplay();
    const int filled =
        std::max(0, std::min(kTimerBarSegments,
                             static_cast<int>(remainingFraction * kTimerBarSegments + 0.5f)));

    char bar[kTimerBarSegments + 1];
    for (int i = 0; i < kTimerBarSegments; ++i) {
        bar[i] = (i < filled) ? '=' : ' ';
    }
    bar[kTimerBarSegments] = '\0';

    display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    display->drawText(bar, 0, kTimerBarY);
}

void BonkItSession::beginCue(FDN* fdn, BonkCue cue, unsigned long durationMs, bool showTimerBar) {
    activeCue_        = cue;
    result_           = BonkRoundResult::NONE;
    roundActive_      = true;
    roundDurationMs_  = durationMs;
    showTimerBar_     = showTimerBar;
    if (durationMs > 0) {
        roundTimer_.setTimer(durationMs);
    } else {
        roundTimer_.invalidate();
    }
    renderActiveRound(fdn);
}

void BonkItSession::renderActiveRound(FDN* fdn) {
    if (!roundActive_) {
        return;
    }

    renderCueGlyph(fdn, activeCue_);
    if (showTimerBar_) {
        renderTimerBar(fdn, roundTimeRemainingFraction());
    }
    fdn->getDisplay()->render();
}

float BonkItSession::roundTimeRemainingFraction() {
    if (!roundActive_ || !roundTimer_.isRunning()) {
        return 0.0f;
    }

    const unsigned long elapsed = roundTimer_.getElapsedTime();
    const unsigned long total   = roundDurationMs_;
    if (total == 0) {
        return 0.0f;
    }

    if (elapsed >= total) {
        return 0.0f;
    }

    return 1.0f - static_cast<float>(elapsed) / static_cast<float>(total);
}

void BonkItSession::setFadeLightsSpeed(FDN* fdn, int speedMs) {
    LightManager* lightManager = fdn->getLightManager();
    const uint8_t speed = static_cast<uint8_t>(speedMs);

    if (lightManager->isAnimating()) {
        lightManager->setAnimationSpeed(speed);
        return;
    }

    AnimationConfig config;
    config.loop         = true;
    config.speed        = speed;
    config.loopDelayMs  = 0;
    config.initialState = LEDState();
    lightManager->startAnimation(new FdnBonkFadeAnimation(), config);
}

void BonkItSession::stopFadeLights(FDN* fdn) {
    fdn->getLightManager()->stopAnimation();
    fdn->getLightManager()->clear();
}

void BonkItSession::update(FDN* fdn) {
    if (roundActive_ && roundTimer_.isRunning()) {
        roundTimer_.updateTime();
        renderActiveRound(fdn);

        if (roundTimer_.expired()) {
            if (activeCue_ == BonkCue::X) {
                resolveRound(BonkRoundResult::CORRECT);
            } else {
                resolveRound(BonkRoundResult::TIMEOUT);
            }
        }
    } else if (roundActive_) {
        renderActiveRound(fdn);
    }

    updateLossFeedback(fdn);

    if (showingLoseAnimation_) {
        loseAnimationTimer_.updateTime();
    }
}

bool BonkItSession::isCorrectPress(ButtonIdentifier buttonId) const {
    switch (activeCue_) {
        case BonkCue::UP:
            return buttonId == ButtonIdentifier::PRIMARY_BUTTON;
        case BonkCue::DOWN:
            return buttonId == ButtonIdentifier::SECONDARY_BUTTON;
        case BonkCue::X:
            return false;
    }
    return false;
}

BonkRoundResult BonkItSession::onButtonPress(ButtonIdentifier buttonId) {
    if (!roundActive_ || result_ != BonkRoundResult::NONE) {
        return BonkRoundResult::NONE;
    }

    if (activeCue_ == BonkCue::X) {
        resolveRound(BonkRoundResult::WRONG);
        return result_;
    }

    if (isCorrectPress(buttonId)) {
        resolveRound(BonkRoundResult::CORRECT);
    } else {
        resolveRound(BonkRoundResult::WRONG);
    }

    return result_;
}

BonkRoundResult BonkItSession::pollResult() {
    const BonkRoundResult current = result_;
    result_ = BonkRoundResult::NONE;
    return current;
}

void BonkItSession::resolveRound(BonkRoundResult result) {
    result_      = result;
    roundActive_ = false;
    roundTimer_.invalidate();
}

void BonkItSession::pulseCorrectFeedback() {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_HAPTIC, 0, 1);
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_LED, 0, 1);
}

void BonkItSession::beginLossFeedback() {
    lossPulseActive_ = true;
    lossPulsesSent_  = 0;
    lossPulseOn_     = true;
    lossPulseTimer_.setTimer(kLossPulseDurationMs);

    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_HAPTIC, 0, 3);
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_LED, 0, 3);
}

void BonkItSession::updateLossFeedback(FDN* fdn) {
    (void)fdn;

    if (!lossPulseActive_) {
        return;
    }

    lossPulseTimer_.updateTime();
    if (!lossPulseTimer_.expired()) {
        return;
    }

    if (lossPulseOn_) {
        lossPulseOn_ = false;
        lossPulsesSent_++;
        lossPulseTimer_.setTimer(kLossPulseGapMs);
        return;
    }

    if (lossPulsesSent_ >= lossPulsesTarget_) {
        lossPulseActive_ = false;
        lossPulseTimer_.invalidate();
        return;
    }

    lossPulseOn_ = true;
    lossPulseTimer_.setTimer(kLossPulseDurationMs);
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_HAPTIC, 0, 3);
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::BLINK_LED, 0, 3);
}

bool BonkItSession::lossFeedbackComplete() const {
    return !lossPulseActive_;
}

void BonkItSession::sendPeripheralAnimation(uint8_t animationId) {
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::DISPLAY_ANIMATION, animationId, 0);
}

void BonkItSession::renderLossMessage(FDN* fdn) {
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText("YOU LOSE!", 32)
        ->render();
}

void BonkItSession::showLoseAnimation(FDN* fdn) {
    showingLoseAnimation_ = true;
    renderLossMessage(fdn);
    sendPeripheralAnimation(static_cast<uint8_t>(BonkItPeripheral::AnimationId::LOSE));

    AnimationConfig config;
    config.loop         = true;
    config.speed        = 16;
    config.loopDelayMs  = 0;
    config.initialState = LEDState();
    fdn->getLightManager()->startAnimation(new FDNLoseAnimation(), config);
    loseAnimationTimer_.setTimer(kLoseAnimationMs);
}

bool BonkItSession::loseAnimationComplete() {
    return showingLoseAnimation_ && loseAnimationTimer_.isRunning() && loseAnimationTimer_.expired();
}

void BonkItSession::clearAnimations(FDN* fdn) {
    controllerWirelessManager_->sendPeripheralCommandPacket(PeripheralCmd::CLEAR_LEDS, 0, 0);
    controllerWirelessManager_->sendPeripheralCommandPacket(PeripheralCmd::CLEAR_DISPLAY, 0, 0);
    fdn->getLightManager()->stopAnimation();
    fdn->getLightManager()->clear();
    showingLoseAnimation_ = false;
    loseAnimationTimer_.invalidate();
    lossPulseActive_ = false;
    lossPulseTimer_.invalidate();
}
