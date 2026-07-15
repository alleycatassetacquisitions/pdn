#pragma once

#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "game/bonk-it-glyphs.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include <cstddef>

enum class BonkCue : uint8_t {
    UP = 0,
    DOWN = 1,
    X = 2,
};

enum class BonkRoundResult : uint8_t {
    NONE,
    CORRECT,
    WRONG,
    TIMEOUT,
};

class BonkItSession {
public:
    explicit BonkItSession(ControllerWirelessManager* controllerWirelessManager);

    void reset();

    void renderTutorialMessage(FDN* fdn, const char* line1);
    void renderTutorialMessage(FDN* fdn, const char* line1, const char* line2);
    void renderTutorialMessage(FDN* fdn, const char* line1, const char* line2, const char* line3);
    void renderFeedbackMessage(FDN* fdn, const char* line);
    void renderReadyMessage(FDN* fdn);

    void beginCue(FDN* fdn, BonkCue cue, unsigned long durationMs, bool showTimerBar = true);
    void setFadeLightsSpeed(FDN* fdn, int speedMs);
    void stopFadeLights(FDN* fdn);

    void update(FDN* fdn);

    BonkRoundResult onButtonPress(ButtonIdentifier buttonId);
    BonkRoundResult pollResult();

    void renderActiveRound(FDN* fdn);
    float roundTimeRemainingFraction();

    bool isRoundActive() const { return roundActive_; }
    BonkCue activeCue() const { return activeCue_; }

    void pulseCorrectFeedback();
    void beginLossFeedback();
    void updateLossFeedback(FDN* fdn);
    bool lossFeedbackComplete() const;

    void showLoseAnimation(FDN* fdn);
    void renderLossMessage(FDN* fdn);
    bool loseAnimationComplete();
    void clearAnimations(FDN* fdn);

    static const char* glyphForCue(BonkCue cue);
    static unsigned long durationForRound(int roundIndex);
    static int fadeSpeedForRound(int roundIndex);
    static BonkCue randomCue();

private:
    ControllerWirelessManager* controllerWirelessManager_;

    bool roundActive_ = false;
    BonkCue activeCue_ = BonkCue::UP;
    BonkRoundResult result_ = BonkRoundResult::NONE;
    unsigned long roundDurationMs_ = 0;
    bool showTimerBar_ = true;
    SimpleTimer roundTimer_;

    bool lossPulseActive_ = false;
    int lossPulsesSent_ = 0;
    int lossPulsesTarget_ = 5;
    bool lossPulseOn_ = false;
    SimpleTimer lossPulseTimer_;

    bool showingLoseAnimation_ = false;
    SimpleTimer loseAnimationTimer_;

    void renderCueGlyph(FDN* fdn, BonkCue cue);
    void renderTimerBar(FDN* fdn, float remainingFraction);
    bool isCorrectPress(ButtonIdentifier buttonId) const;
    void resolveRound(BonkRoundResult result);
    void sendPeripheralAnimation(uint8_t animationId);
};
