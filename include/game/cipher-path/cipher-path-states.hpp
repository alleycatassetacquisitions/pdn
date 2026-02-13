#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class CipherPath;

/*
 * Cipher Path state IDs — offset to 500+ to avoid collisions.
 */
enum CipherPathStateId {
    CIPHER_INTRO = 500,
    CIPHER_WIN   = 501,
    CIPHER_LOSE  = 502,
};

/*
 * CipherPathIntro — Title screen. Shows "CIPHER PATH" and subtitle.
 * Auto-transitions to CipherPathWin after INTRO_DURATION_MS (stub behavior).
 */
class CipherPathIntro : public State {
public:
    explicit CipherPathIntro(CipherPath* game);
    ~CipherPathIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToWin();

private:
    CipherPath* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToWinState = false;
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
    bool isTerminalState() override;

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
    bool isTerminalState() override;

private:
    CipherPath* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
