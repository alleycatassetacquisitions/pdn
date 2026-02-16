#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class SpikeVector;

/*
 * Spike Vector state IDs — offset to 400+ to avoid collisions.
 */
enum SpikeVectorStateId {
    SPIKE_INTRO    = 400,
    SPIKE_WIN      = 401,
    SPIKE_LOSE     = 402,
    SPIKE_SHOW     = 403,
    SPIKE_GAMEPLAY = 404,
    SPIKE_EVALUATE = 405,
};

/*
 * SpikeVectorIntro — Title screen. Shows "SPIKE VECTOR" and subtitle.
 * Resets session, seeds RNG. Transitions to SpikeVectorShow after timer.
 */
class SpikeVectorIntro : public State {
public:
    explicit SpikeVectorIntro(SpikeVector* game);
    ~SpikeVectorIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShow();

private:
    SpikeVector* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToShowState = false;
};

/*
 * SpikeVectorShow — Level info screen with progress pips.
 * Shows level progress (●●●○○) with newly-completed pip flashing.
 * Generates gap positions array for all walls in the level.
 * Transitions to SpikeVectorGameplay after flash animation.
 */
class SpikeVectorShow : public State {
public:
    explicit SpikeVectorShow(SpikeVector* game);
    ~SpikeVectorShow() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToGameplay();

private:
    SpikeVector* game;
    SimpleTimer showTimer;
    SimpleTimer flashTimer;
    int flashCount = 0;
    bool flashState = false;
    static constexpr int SHOW_DURATION_MS = 1000;  // Total duration for flash sequence
    static constexpr int FLASH_INTERVAL_MS = 150;  // Toggle interval
    bool transitionToGameplayState = false;
};

/*
 * SpikeVectorGameplay — Side-scrolling wall gauntlet.
 * Multi-wall formation slides left. Player dodges through gaps.
 * Primary button = move cursor UP (decrease position).
 * Secondary button = move cursor DOWN (increase position).
 * Collision detected inline per-wall. Transitions to Evaluate when all walls pass.
 */
class SpikeVectorGameplay : public State {
public:
    explicit SpikeVectorGameplay(SpikeVector* game);
    ~SpikeVectorGameplay() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToEvaluate();

    SpikeVector* game;

private:
    SimpleTimer moveTimer;
    SimpleTimer hitFlashTimer;
    bool hitFlashActive = false;
    int hitFlashCount = 0;
    bool transitionToEvaluateState = false;

    // Rendering helpers
    void renderFrame(Device* PDN);
    void renderHUD(Device* PDN);
    void renderGameArea(Device* PDN);
    void renderControls(Device* PDN);
    void handleCollisions(Device* PDN);
    void triggerHitFeedback(Device* PDN);
};

/*
 * SpikeVectorEvaluate — Level-complete logic.
 * Checks if all levels completed (win), too many hits (lose), or continue (next level).
 * Collision already handled inline during Gameplay — this just routes outcomes.
 */
class SpikeVectorEvaluate : public State {
public:
    explicit SpikeVectorEvaluate(SpikeVector* game);
    ~SpikeVectorEvaluate() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShow();
    bool transitionToWin();
    bool transitionToLose();

private:
    SpikeVector* game;
    bool transitionToShowState = false;
    bool transitionToWinState = false;
    bool transitionToLoseState = false;
};

/*
 * SpikeVectorWin — Victory screen. Shows "VECTOR CLEAR".
 * Sets outcome to WON. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class SpikeVectorWin : public State {
public:
    explicit SpikeVectorWin(SpikeVector* game);
    ~SpikeVectorWin() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() const override;

private:
    SpikeVector* game;
    SimpleTimer winTimer;
    static constexpr int WIN_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};

/*
 * SpikeVectorLose — Defeat screen. Shows "SPIKE IMPACT".
 * Sets outcome to LOST. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class SpikeVectorLose : public State {
public:
    explicit SpikeVectorLose(SpikeVector* game);
    ~SpikeVectorLose() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() const override;

private:
    SpikeVector* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
