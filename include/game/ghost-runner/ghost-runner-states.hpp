#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class GhostRunner;

/*
 * Ghost Runner state IDs — offset to 300+ to avoid collisions.
 */
enum GhostRunnerStateId {
    GHOST_INTRO = 300,
    GHOST_WIN   = 301,
    GHOST_LOSE  = 302,
};

/*
 * GhostRunnerIntro — Title screen. Shows "GHOST RUNNER" and subtitle.
 * Auto-transitions to GhostRunnerWin after INTRO_DURATION_MS (stub behavior).
 */
class GhostRunnerIntro : public State {
public:
    explicit GhostRunnerIntro(GhostRunner* game);
    ~GhostRunnerIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToWin();

private:
    GhostRunner* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToWinState = false;
};

/*
 * GhostRunnerWin — Victory screen. Shows "RUN COMPLETE".
 * Sets outcome to WON. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class GhostRunnerWin : public State {
public:
    explicit GhostRunnerWin(GhostRunner* game);
    ~GhostRunnerWin() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() override;

private:
    GhostRunner* game;
    SimpleTimer winTimer;
    static constexpr int WIN_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};

/*
 * GhostRunnerLose — Defeat screen. Shows "GHOST CAUGHT".
 * Sets outcome to LOST. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class GhostRunnerLose : public State {
public:
    explicit GhostRunnerLose(GhostRunner* game);
    ~GhostRunnerLose() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() override;

private:
    GhostRunner* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
