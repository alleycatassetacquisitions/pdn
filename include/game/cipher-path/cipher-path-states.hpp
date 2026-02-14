#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class CipherPath;

/*
 * Cipher Path state IDs — offset to 500+ to avoid collisions.
 */
enum CipherPathStateId {
    CIPHER_INTRO    = 500,
    CIPHER_WIN      = 501,
    CIPHER_LOSE     = 502,
    CIPHER_SHOW     = 503,
    CIPHER_GAMEPLAY = 504,
    CIPHER_EVALUATE = 505,
};

/*
 * CipherPathIntro — Title screen. Shows "CIPHER PATH" and subtitle.
 * Resets session, seeds RNG. Transitions to CipherPathShow.
 */
class CipherPathIntro : public State {
public:
    explicit CipherPathIntro(CipherPath* game);
    ~CipherPathIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShow();

private:
    CipherPath* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToShowState = false;
};

/*
 * CipherPathShow — Round setup screen. Displays "Round X of Y".
 * Generates the cipher for this round. Resets position and moves.
 * Transitions to CipherPathGameplay after SHOW_DURATION_MS.
 */
class CipherPathShow : public State {
public:
    explicit CipherPathShow(CipherPath* game);
    ~CipherPathShow() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToGameplay();

private:
    CipherPath* game;
    SimpleTimer showTimer;
    static constexpr int SHOW_DURATION_MS = 1500;
    bool transitionToGameplayState = false;
};

/*
 * CipherPathGameplay — Core gameplay. Player presses UP/DOWN each step.
 * Correct direction advances position, wrong wastes a move.
 * Transitions to CipherPathEvaluate when exit reached or budget exhausted.
 */
class CipherPathGameplay : public State {
public:
    explicit CipherPathGameplay(CipherPath* game);
    ~CipherPathGameplay() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToEvaluate();

    // Called from button callbacks to signal end condition
    void setNeedsEvaluation() { needsEvaluation = true; }

private:
    CipherPath* game;
    bool transitionToEvaluateState = false;
    bool needsEvaluation = false;
    bool displayIsDirty = false;

    void renderGameplayScreen(Device* PDN);
};

/*
 * CipherPathEvaluate — Brief transition state. Checks round outcome:
 * - If player did not reach exit (budget exhausted): transition to Lose
 * - If all rounds completed: transition to Win
 * - Otherwise: advance round, transition to Show for next round
 */
class CipherPathEvaluate : public State {
public:
    explicit CipherPathEvaluate(CipherPath* game);
    ~CipherPathEvaluate() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShow();
    bool transitionToWin();
    bool transitionToLose();

private:
    CipherPath* game;
    bool transitionToShowState = false;
    bool transitionToWinState = false;
    bool transitionToLoseState = false;
};

/*
 * CipherPathWin — Victory screen. Shows "PATH DECODED".
 * Sets outcome to WON. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class CipherPathWin : public State {
public:
    explicit CipherPathWin(CipherPath* game);
    ~CipherPathWin() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() const override;

private:
    CipherPath* game;
    SimpleTimer winTimer;
    static constexpr int WIN_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};

/*
 * CipherPathLose — Defeat screen. Shows "PATH LOST".
 * Sets outcome to LOST. In standalone mode, transitions back to Intro.
 * In managed mode, calls Device::returnToPreviousApp().
 */
class CipherPathLose : public State {
public:
    explicit CipherPathLose(CipherPath* game);
    ~CipherPathLose() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() const override;

private:
    CipherPath* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
