#pragma once

#include "game/minigame.hpp"
#include "utils/simple-timer.hpp"
#include <vector>
#include <cstdlib>

/*
 * Signal Echo configuration — all tuning variables for difficulty.
 * Two presets are defined in signal-echo-resources.hpp:
 * SIGNAL_ECHO_EASY (default) and SIGNAL_ECHO_HARD.
 */
struct SignalEchoConfig {
    int sequenceLength = 4;
    int numSequences = 3;
    int displaySpeedMs = 600;
    int timeLimitMs = 0;
    bool cumulative = false;
    int allowedMistakes = 2;
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

/*
 * Signal Echo session state — tracks the current game in progress.
 * Reset between games (on lose/restart). Win/lose is tracked via
 * MiniGame::outcome, not duplicated here.
 */
struct SignalEchoSession {
    std::vector<bool> currentSequence;
    int currentRound = 0;
    int inputIndex = 0;
    int mistakes = 0;
    int score = 0;

    void reset() {
        currentSequence.clear();
        currentRound = 0;
        inputIndex = 0;
        mistakes = 0;
        score = 0;
    }
};

constexpr int SIGNAL_ECHO_APP_ID = 2;

/*
 * Signal Echo (Simon Says) — the first MiniGame implementation.
 *
 * The device plays a sequence of UP/DOWN signals, then the player
 * reproduces it from memory. Correct replay advances to the next round.
 * Wrong input counts as a mistake. Too many mistakes = lose.
 * Complete all rounds = win.
 *
 * In standalone mode, sequences are generated locally using a seeded
 * PRNG. In managed mode, sequences come from the NPC via serial.
 * Terminal states call Device::returnToPreviousApp() in managed mode,
 * or loop back to EchoIntro for standalone replay.
 */
class SignalEcho : public MiniGame {
public:
    explicit SignalEcho(SignalEchoConfig config);
    void populateStateMap() override;
    void resetGame() override;

    SignalEchoConfig& getConfig() { return config; }
    SignalEchoSession& getSession() { return session; }

    /*
     * Generate a sequence of the given length using the seeded PRNG.
     * true = UP, false = DOWN.
     */
    std::vector<bool> generateSequence(int length);

    /*
     * Seed the PRNG. Called once during initialization.
     * If config.rngSeed != 0, uses that seed (deterministic for tests).
     * Otherwise uses 0 (production would use MAC address, but standalone
     * mode falls back to time-based or 0).
     */
    void seedRng();

private:
    SignalEchoConfig config;
    SignalEchoSession session;
};
