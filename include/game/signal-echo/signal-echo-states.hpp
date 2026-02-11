#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"

class SignalEcho;

/*
 * Signal Echo state IDs — used for state tracking and test assertions.
 */
enum SignalEchoStateId {
    ECHO_INTRO = 100,
    ECHO_SHOW_SEQUENCE = 101,
    ECHO_PLAYER_INPUT = 102,
    ECHO_EVALUATE = 103,
    ECHO_WIN = 104,
    ECHO_LOSE = 105,
};

/*
 * EchoIntro — Title screen. Shows "SIGNAL ECHO" and "Watch. Repeat."
 * Transitions to EchoShowSequence after INTRO_DURATION_MS.
 * Also seeds the RNG and generates the first sequence.
 */
class EchoIntro : public State {
public:
    explicit EchoIntro(SignalEcho* game);
    ~EchoIntro() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShowSequence();

private:
    SignalEcho* game;
    SimpleTimer introTimer;
    static constexpr int INTRO_DURATION_MS = 2000;
    bool transitionToShowSequenceState = false;
};

/*
 * EchoShowSequence — Displays the sequence one signal at a time.
 * Each signal (UP or DOWN) is shown for displaySpeedMs.
 * After the last signal, transitions to EchoPlayerInput.
 *
 * Display: Large arrow (^ or v) + "Signal N of M"
 * LEDs: Chase up for UP, chase down for DOWN
 * Haptics: Light pulse per signal
 */
class EchoShowSequence : public State {
public:
    explicit EchoShowSequence(SignalEcho* game);
    ~EchoShowSequence() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToPlayerInput();

private:
    SignalEcho* game;
    SimpleTimer signalTimer;
    int currentSignalIndex = 0;
    bool transitionToPlayerInputState = false;
    bool displayedCurrentSignal = false;

    void displaySignal(Device* PDN, bool isUp, int index, int total);
};

/*
 * EchoPlayerInput — Player reproduces the sequence using buttons.
 * Primary button = UP, Secondary button = DOWN.
 *
 * Correct input: advances inputIndex, adds score.
 * Wrong input: increments mistakes, advances inputIndex (locked in).
 *
 * Transitions to EchoEvaluate when all inputs entered OR when
 * mistakes exceed allowedMistakes.
 *
 * Display: "YOUR TURN" + input progress + round counter + life indicator
 */
class EchoPlayerInput : public State {
public:
    explicit EchoPlayerInput(SignalEcho* game);
    ~EchoPlayerInput() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToEvaluate();

private:
    SignalEcho* game;
    bool transitionToEvaluateState = false;
    bool displayIsDirty = false;

    void handleInput(bool isUp, Device* PDN);
    void renderInputScreen(Device* PDN);
};

/*
 * EchoEvaluate — Brief transition state. Checks round outcome:
 * - If mistakes > allowedMistakes: transition to EchoLose
 * - If all rounds completed: transition to EchoWin
 * - Otherwise: advance to next round, transition to EchoShowSequence
 */
class EchoEvaluate : public State {
public:
    explicit EchoEvaluate(SignalEcho* game);
    ~EchoEvaluate() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToShowSequence();
    bool transitionToWin();
    bool transitionToLose();

private:
    SignalEcho* game;
    bool transitionToShowSequenceState = false;
    bool transitionToWinState = false;
    bool transitionToLoseState = false;
};

/*
 * EchoWin — Victory screen. Shows "ACCESS GRANTED" + score.
 * Sets outcome to WON. In standalone mode, transitions back to EchoIntro.
 * In managed mode, calls Device::returnToPreviousApp().
 *
 * LEDs: Rainbow sweep
 * Haptics: Celebration pattern
 */
class EchoWin : public State {
public:
    explicit EchoWin(SignalEcho* game);
    ~EchoWin() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() override;

private:
    SignalEcho* game;
    SimpleTimer winTimer;
    static constexpr int WIN_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};

/*
 * EchoLose — Defeat screen. Shows "SIGNAL LOST".
 * Sets outcome to LOST. In standalone mode, transitions back to EchoIntro.
 * In managed mode, calls Device::returnToPreviousApp().
 *
 * LEDs: Red fade
 * Haptics: Long buzz
 */
class EchoLose : public State {
public:
    explicit EchoLose(SignalEcho* game);
    ~EchoLose() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIntro();
    bool isTerminalState() override;

private:
    SignalEcho* game;
    SimpleTimer loseTimer;
    static constexpr int LOSE_DISPLAY_MS = 3000;
    bool transitionToIntroState = false;
};
