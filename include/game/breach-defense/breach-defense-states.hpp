#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "game/base-states/base-intro-state.hpp"
#include "game/base-states/base-win-state.hpp"
#include "game/base-states/base-lose-state.hpp"

class BreachDefense;

/*
 * Breach Defense state IDs — offset to 700+ to avoid collisions.
 */
enum BreachDefenseStateId {
    BREACH_INTRO    = 700,
    BREACH_WIN      = 701,
    BREACH_LOSE     = 702,
    BREACH_SHOW     = 703,
    BREACH_GAMEPLAY = 704,
    BREACH_EVALUATE = 705,
};

/*
 * BreachDefenseIntro — Title screen. Shows "BREACH DEFENSE" and subtitle.
 * Resets session and seeds RNG. Transitions to Gameplay after timer.
 */
class BreachDefenseIntro : public BaseIntroState<BreachDefense> {
public:
    explicit BreachDefenseIntro(BreachDefense* game);

    bool transitionToGameplay() const {
        return shouldTransition();
    }
};

/*
 * BreachDefenseGameplay — Core combo defense gameplay.
 * Threats spawn on fixed rhythm and overlap on screen.
 * Player moves shield with UP/DOWN buttons to block threats at defense line.
 * Inline evaluation: BLOCKED (+100 score, XOR flash, light haptic) or BREACH (breaches++, invert flash, heavy haptic).
 * Transitions to Win (all threats blocked with acceptable breaches) or Lose (too many breaches).
 */
class BreachDefenseGameplay : public State {
public:
    explicit BreachDefenseGameplay(BreachDefense* game);
    ~BreachDefenseGameplay() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToWin();
    bool transitionToLose();

    BreachDefense* game;

private:
    SimpleTimer spawnTimer;
    SimpleTimer threatTimers[3];
    bool transitionToWinState = false;
    bool transitionToLoseState = false;

    // Rendering helpers
    void drawFrame(Device* PDN);
    void drawHUD(Device* PDN);
    void drawLanes(Device* PDN);
    void drawDefenseLine(Device* PDN);
    void drawShield(Device* PDN);
    void drawThreats(Device* PDN);
    void drawControls(Device* PDN);
};

/*
 * BreachDefenseWin — Victory screen. Shows "BREACH BLOCKED".
 * Sets outcome to WON. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class BreachDefenseWin : public BaseWinState<BreachDefense> {
public:
    explicit BreachDefenseWin(BreachDefense* game);

    bool transitionToIntro() const {
        return shouldTransition();
    }
};

/*
 * BreachDefenseLose — Defeat screen. Shows "BREACH OPEN".
 * Sets outcome to LOST. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class BreachDefenseLose : public BaseLoseState<BreachDefense> {
public:
    explicit BreachDefenseLose(BreachDefense* game);

    bool transitionToIntro() const {
        return shouldTransition();
    }
};
