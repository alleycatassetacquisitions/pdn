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
 * CipherPathIntro — Title screen. Shows "CIPHER PATH" and "Route the signal."
 * Brief circuit animation (random wires flash on/off).
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
    SimpleTimer animTimer;        // For flashing circuit animation
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToShowState = false;
};

/*
 * CipherPathShow — Round setup screen. Displays grid with all tiles in scrambled orientations.
 * Round pips shown for hard mode. Brief preview (~2s) for the player to plan.
 * Generates the path and noise tiles. Resets flow and cursor state.
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
    static constexpr int SHOW_DURATION_MS = 2000;
    bool transitionToGameplayState = false;

    void renderShowScreen(Device* PDN);
    void renderGrid(Device* PDN, int gridX, int gridY);
    void renderTile(Device* PDN, int cellX, int cellY, int cellIndex);
    void renderWireTile(Device* PDN, int x, int y, int tileType, int rotation, int wireWidth);
};

/*
 * CipherPathGameplay — Core gameplay. Player rotates wire tiles to complete circuit.
 * UP (primary) = navigate to next tile
 * DOWN (secondary) = rotate current tile 90° clockwise
 * Electricity flows pixel-by-pixel from input terminal. If it reaches a disconnection,
 * the puzzle fails (circuit break). If it reaches the output terminal, the round wins.
 * Transitions to CipherPathEvaluate when circuit completes or breaks.
 */
class CipherPathGameplay : public State {
public:
    explicit CipherPathGameplay(CipherPath* game);
    ~CipherPathGameplay() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToEvaluate();

    // Called from button callbacks
    void setNeedsEvaluation() { needsEvaluation = true; }
    void setCircuitBreak() { circuitBroken = true; }

private:
    CipherPath* game;
    bool transitionToEvaluateState = false;
    bool needsEvaluation = false;
    bool circuitBroken = false;
    bool displayIsDirty = true;

    SimpleTimer flowTimer;           // Controls electricity advancement speed
    SimpleTimer cursorBlinkTimer;    // Cursor blink animation (~300ms toggle)
    bool cursorVisible = true;

    void renderGameplayScreen(Device* PDN);
    void renderGrid(Device* PDN, int gridX, int gridY);
    void renderTile(Device* PDN, int cellX, int cellY, int cellIndex, bool isCursor, bool isEnergized, int flowPixel);
    void renderWireTile(Device* PDN, int x, int y, int tileType, int rotation, int wireWidth, bool invert);
    void advanceFlow(Device* PDN);  // Move electricity forward one pixel
    bool checkConnection(int fromIndex, int toIndex);  // Validate wire connection
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
 * CipherPathWin — Victory screen. Shows "CIRCUIT ROUTED".
 * Power wave cascade animation. Sets outcome to WON.
 * In standalone mode, transitions back to Intro.
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
 * CipherPathLose — Defeat screen. Shows "CIRCUIT BREAK".
 * Spark animation at disconnection point + full haptic surge (255) + screen flash.
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
    SimpleTimer sparkTimer;       // Spark animation timing
    int sparkFlashCount = 0;      // Track spark flash cycles
    static constexpr int LOSE_DISPLAY_MS = 3000;
    static constexpr int SPARK_FLASH_MS = 130;  // ~130ms per flash
    bool transitionToIntroState = false;
};
