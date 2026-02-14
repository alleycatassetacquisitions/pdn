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
 * SpikeVectorShow — Wave info screen. Shows "Wave X of Y" and lives.
 * Generates gap position for this wave, resets wall.
 * Transitions to SpikeVectorGameplay after SHOW_DURATION_MS.
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
    static constexpr int SHOW_DURATION_MS = 1500;
    bool transitionToGameplayState = false;
};

/*
 * SpikeVectorGameplay — Active dodging. Wall advances on timer.
 * Primary button = move cursor UP (decrease position).
 * Secondary button = move cursor DOWN (increase position).
 * When wall reaches trackLength, transitions to SpikeVectorEvaluate.
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
    SimpleTimer wallTimer;
    bool transitionToEvaluateState = false;
};

/*
 * SpikeVectorEvaluate — Checks dodge result.
 * If cursor == gap: dodge (score += 100).
 * If cursor != gap: hit (hits++).
 * Routes to Show (next wave), Win (all waves cleared), or Lose (too many hits).
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
