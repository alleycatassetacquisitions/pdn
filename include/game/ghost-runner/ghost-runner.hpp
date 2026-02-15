#pragma once

#include "game/minigame.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include <cstdlib>

constexpr int GHOST_RUNNER_APP_ID = 4;

struct GhostRunnerConfig {
    int ghostSpeedMs = 50;        // ms per position step
    int screenWidth = 100;        // ghost moves 0 to screenWidth
    int targetZoneStart = 40;     // target zone start position
    int targetZoneEnd = 60;       // target zone end position
    int rounds = 4;               // total rounds to complete
    int missesAllowed = 2;        // strikes before losing
    unsigned long rngSeed = 0;    // 0 = random, nonzero = deterministic
    bool managedMode = false;
};

struct GhostRunnerSession {
    int ghostPosition = 0;
    int currentRound = 0;
    int strikes = 0;
    int score = 0;
    bool playerPressed = false;
    void reset() {
        ghostPosition = 0;
        currentRound = 0;
        strikes = 0;
        score = 0;
        playerPressed = false;
    }
};

inline GhostRunnerConfig makeGhostRunnerEasyConfig() {
    GhostRunnerConfig c;
    c.ghostSpeedMs = 50;
    c.screenWidth = 100;
    c.targetZoneStart = 35;
    c.targetZoneEnd = 65;  // wide zone (30 units)
    c.rounds = 4;
    c.missesAllowed = 3;
    return c;
}

inline GhostRunnerConfig makeGhostRunnerHardConfig() {
    GhostRunnerConfig c;
    c.ghostSpeedMs = 30;
    c.screenWidth = 100;
    c.targetZoneStart = 42;
    c.targetZoneEnd = 58;  // narrow zone (16 units)
    c.rounds = 6;
    c.missesAllowed = 1;
    return c;
}

const GhostRunnerConfig GHOST_RUNNER_EASY = makeGhostRunnerEasyConfig();
const GhostRunnerConfig GHOST_RUNNER_HARD = makeGhostRunnerHardConfig();

/*
 * Ghost Runner — timing/reaction minigame.
 *
 * A ghost scrolls across the screen (position 0 to screenWidth).
 * Player presses PRIMARY button when ghost is in the target zone.
 * Hit = advance round, miss (press outside zone) = strike.
 * If ghost reaches end without press = also a strike (timeout).
 * Complete all rounds to win, exceed strikes to lose.
 *
 * In managed mode (via FDN), terminal states call
 * Device::returnToPreviousApp(). In standalone mode, loops to intro.
 */
class GhostRunner : public MiniGame {
public:
    explicit GhostRunner(GhostRunnerConfig config) :
        MiniGame(GHOST_RUNNER_APP_ID, GameType::GHOST_RUNNER, "GHOST RUNNER"),
        config(config)
    {
    }

    void populateStateMap() override;
    void resetGame() override;

    GhostRunnerConfig& getConfig() { return config; }
    GhostRunnerSession& getSession() { return session; }

private:
    GhostRunnerConfig config;
    GhostRunnerSession session;
};
