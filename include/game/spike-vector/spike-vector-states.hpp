#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class SpikeVector;

/*
 * Spike Vector state IDs — offset to 400+ to avoid collisions.
 */
enum SpikeVectorStateId {
    SPIKE_INTRO = 400,
    SPIKE_WIN   = 401,
    SPIKE_LOSE  = 402,
};

/*
 * SpikeVectorIntro — Title screen. Shows "SPIKE VECTOR" and subtitle.
 * Auto-transitions to SpikeVectorWin after INTRO_DURATION_MS (stub behavior).
 */
class SpikeVectorIntro : public State {
public:
    explicit SpikeVectorIntro(SpikeVector* game);
    ~SpikeVectorIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToWin();

private:
    SpikeVector* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToWinState = false;
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
    bool isTerminalState() override;

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
    bool isTerminalState() override;

private:
    SpikeVector* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
