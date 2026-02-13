#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class BreachDefense;

/*
 * Breach Defense state IDs — offset to 700+ to avoid collisions.
 */
enum BreachDefenseStateId {
    BREACH_INTRO = 700,
    BREACH_WIN   = 701,
    BREACH_LOSE  = 702,
};

/*
 * BreachDefenseIntro — Title screen. Shows "BREACH DEFENSE" and subtitle.
 * Auto-transitions to BreachDefenseWin after INTRO_DURATION_MS (stub behavior).
 */
class BreachDefenseIntro : public State {
public:
    explicit BreachDefenseIntro(BreachDefense* game);
    ~BreachDefenseIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToWin();

private:
    BreachDefense* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToWinState = false;
};

/*
 * BreachDefenseWin — Victory screen. Shows "BREACH BLOCKED".
 * Sets outcome to WON. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class BreachDefenseWin : public State {
public:
    explicit BreachDefenseWin(BreachDefense* game);
    ~BreachDefenseWin() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() override;

private:
    BreachDefense* game;
    SimpleTimer winTimer;
    static constexpr int WIN_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};

/*
 * BreachDefenseLose — Defeat screen. Shows "BREACH OPEN".
 * Sets outcome to LOST. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class BreachDefenseLose : public State {
public:
    explicit BreachDefenseLose(BreachDefense* game);
    ~BreachDefenseLose() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() override;

private:
    BreachDefense* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
