#pragma once

#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "device/animation/fdn-countdown-animation.hpp"
#include "device/animation/fdn-lose-animation.hpp"
#include "device/animation/fdn-match-success-animation.hpp"
#include "game/quickdraw-countdown.hpp"
#include "game/quickdraw-images.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include <cstdio>

enum class QuickdrawPracticePhase {
    MESSAGE,
    READY,
    COUNTDOWN,
    DRAW,
    OUTCOME,
};

enum class QuickdrawPracticeOutcome {
    NONE,
    EARLY_SHOT,
    WIN,
    LOSE,
    TIMEOUT,
};

class QuickdrawPracticeSession {
public:
    explicit QuickdrawPracticeSession(ControllerWirelessManager* controllerWirelessManager);

    void setOpponentResponseMs(unsigned long opponentResponseMs);
    void setOpponentEnabled(bool enabled);
    void resetMatch();

    void beginReady();
    void beginCountdown();
    void clearOutcome();

    void update(FDN* fdn);

    void renderTutorialMessage(FDN* fdn, const char* line1, const char* line2, const char* line3);
    void renderReady(FDN* fdn, bool showTimeToBeat);
    void renderCountdown(FDN* fdn);
    void renderDraw(FDN* fdn);
    void renderStepImage(FDN* fdn, QuickdrawCountdown::Step step);
    void renderOutcomeImage(FDN* fdn, ImageType imageType);

    void onPlayerShot(bool drawReached);

    QuickdrawPracticePhase phase() const { return phase_; }
    QuickdrawPracticeOutcome outcome() const { return outcome_; }
    bool readyTimerExpired();
    bool outcomeTimerExpired();
    bool matchResolved() const;
    unsigned long playerReactionMs() const { return playerReactionMs_; }
    unsigned long opponentResponseMs() const { return opponentResponseMs_; }

    void showWinAnimation(FDN* fdn);
    void showLoseAnimation(FDN* fdn);
    void clearAnimations(FDN* fdn);
    void pulsePlayerAck();
    void renderRoundResult(FDN* fdn, QuickdrawPracticeOutcome outcome, unsigned long reactionMs);

private:
    ControllerWirelessManager* controllerWirelessManager_;
    QuickdrawPracticePhase phase_ = QuickdrawPracticePhase::MESSAGE;
    QuickdrawPracticeOutcome outcome_ = QuickdrawPracticeOutcome::NONE;

    size_t countdownIndex_ = 0;
    bool drawReached_ = false;
    bool playerShot_ = false;
    unsigned long drawStartMs_ = 0;
    unsigned long playerReactionMs_ = 0;
    unsigned long opponentResponseMs_ = 1000;
    bool opponentEnabled_ = true;

    SimpleTimer readyTimer_;
    SimpleTimer countdownTimer_;
    SimpleTimer duelTimer_;
    SimpleTimer outcomeTimer_;
    SimpleTimer hapticTimer_;

    void advanceCountdown(FDN* fdn);
    void enterDraw(FDN* fdn);
    void resolveDraw(unsigned long reactionMs);
    void syncCountdownStep(FDN* fdn, QuickdrawCountdown::Step step);
    void syncAnimation(FDN* fdn, QuickdrawCountdown::PeripheralAnimationId animationId, uint8_t param2 = 0);
    void pulseHaptic(FDN* fdn);
    void updateHaptic(FDN* fdn);
};
