#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class GhostRunner;

/*
 * Ghost Runner state IDs — offset to 300+ to avoid collisions.
 */
enum GhostRunnerStateId {
    GHOST_INTRO    = 300,
    GHOST_WIN      = 301,
    GHOST_LOSE     = 302,
    GHOST_SHOW     = 303,
    GHOST_GAMEPLAY = 304,
    GHOST_EVALUATE = 305,
};

/*
 * GhostRunnerIntro — Title screen. Shows "GHOST RUNNER" and subtitle.
 * Seeds RNG, resets session, transitions to GhostRunnerShow after timer.
 */
class GhostRunnerIntro : public State {
public:
    explicit GhostRunnerIntro(GhostRunner* game);
    ~GhostRunnerIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShow();

private:
    GhostRunner* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToShowState = false;
};

/*
 * GhostRunnerShow — Preview phase. Generates maze, shows maze layout,
 * then traces solution path with compressed directional sequence.
 * Transitions to GhostRunnerGameplay after preview complete.
 */
class GhostRunnerShow : public State {
public:
    explicit GhostRunnerShow(GhostRunner* game);
    ~GhostRunnerShow() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToGameplay();

private:
    GhostRunner* game;
    SimpleTimer mazePreviewTimer;
    SimpleTimer tracePreviewTimer;
    SimpleTimer traceStepTimer;
    bool showingMaze = true;       // true = maze phase, false = trace phase
    int traceIndex = 0;            // current step in solution trace
    bool transitionToGameplayState = false;

    // Maze rendering helpers
    void drawMaze(Device* PDN, bool showWalls);
    void drawSolutionText(Device* PDN);
};

/*
 * GhostRunnerGameplay — Invisible maze navigation.
 * PRIMARY button: Cycle direction clockwise (UP→RIGHT→DOWN→LEFT)
 * SECONDARY button: Move in current direction
 * Hitting invisible walls costs a life and flashes maze visible.
 * Transitions to GhostRunnerEvaluate when exit reached or lives depleted.
 */
class GhostRunnerGameplay : public State {
public:
    explicit GhostRunnerGameplay(GhostRunner* game);
    ~GhostRunnerGameplay() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToEvaluate();

    bool bonkTriggered = false;  // Set in callback, processed in loop

private:
    GhostRunner* game;
    SimpleTimer mazeFlashTimer;
    SimpleTimer cursorFlashTimer;
    bool transitionToEvaluateState = false;
    bool displayDirty = false;

    // Maze navigation helpers
    bool hasWall(int row, int col, int direction);
    void triggerBonk(Device* PDN);
    void drawMaze(Device* PDN, bool showWalls);
    const char* getDirectionArrow(int dir);
};

/*
 * GhostRunnerEvaluate — Brief transition state. Checks round outcome:
 * - If press was in target zone: hit (advance round, add score)
 * - If press was outside zone or timeout: strike
 * - If strikes > missesAllowed: transition to GhostRunnerLose
 * - If all rounds completed: transition to GhostRunnerWin
 * - Otherwise: advance to next round, transition to GhostRunnerShow
 */
class GhostRunnerEvaluate : public State {
public:
    explicit GhostRunnerEvaluate(GhostRunner* game);
    ~GhostRunnerEvaluate() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShow();
    bool transitionToWin();
    bool transitionToLose();

private:
    GhostRunner* game;
    bool transitionToShowState = false;
    bool transitionToWinState = false;
    bool transitionToLoseState = false;
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
    bool isTerminalState() const override;

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
    bool isTerminalState() const override;

private:
    GhostRunner* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
