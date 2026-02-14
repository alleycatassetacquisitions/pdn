#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class FirewallDecrypt;

/*
 * Firewall Decrypt state IDs — offset to 200+ to avoid collisions.
 */
enum FirewallDecryptStateId {
    DECRYPT_INTRO = 200,
    DECRYPT_SCAN = 201,
    DECRYPT_EVALUATE = 202,
    DECRYPT_WIN = 203,
    DECRYPT_LOSE = 204,
};

/*
 * DecryptIntro — Title screen. Shows "FIREWALL DECRYPT" for 2 seconds.
 * Seeds RNG and sets up the first round.
 */
class DecryptIntro : public State {
public:
    explicit DecryptIntro(FirewallDecrypt* game);
    ~DecryptIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToScan();

private:
    FirewallDecrypt* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToScanState = false;
};

/*
 * DecryptScan — Main gameplay state.
 * Shows target address at top, scrollable candidate list below.
 * Primary = scroll, Secondary = confirm selection.
 * Optional time limit per round (hard mode).
 */
class DecryptScan : public State {
public:
    explicit DecryptScan(FirewallDecrypt* game);
    ~DecryptScan() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToEvaluate();

    int getSelectedIndex() const { return cursorIndex; }

private:
    FirewallDecrypt* game;
    SimpleTimer roundTimer;
    int cursorIndex = 0;
    bool transitionToEvaluateState = false;
    bool displayIsDirty = false;
    bool timedOut = false;

    void renderUi(Device* PDN);
};

/*
 * DecryptEvaluate — Checks the player's selection.
 * Correct: advance round or win.
 * Wrong/timeout: lose.
 */
class DecryptEvaluate : public State {
public:
    explicit DecryptEvaluate(FirewallDecrypt* game);
    ~DecryptEvaluate() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToScan();
    bool transitionToWin();
    bool transitionToLose();

private:
    FirewallDecrypt* game;
    bool transitionToScanState = false;
    bool transitionToWinState = false;
    bool transitionToLoseState = false;
};

/*
 * DecryptWin — Victory screen. Shows "DECRYPTED!" + score.
 * In managed mode, calls returnToPreviousApp.
 */
class DecryptWin : public State {
public:
    explicit DecryptWin(FirewallDecrypt* game);
    ~DecryptWin() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() const override;

private:
    FirewallDecrypt* game;
    SimpleTimer winTimer;
    static constexpr int WIN_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};

/*
 * DecryptLose — Defeat screen. Shows "FIREWALL INTACT".
 * In managed mode, calls returnToPreviousApp.
 */
class DecryptLose : public State {
public:
    explicit DecryptLose(FirewallDecrypt* game);
    ~DecryptLose() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() const override;

private:
    FirewallDecrypt* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
