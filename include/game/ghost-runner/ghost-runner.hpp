#pragma once

#include "game/minigame.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"

constexpr int GHOST_RUNNER_APP_ID = 4;

struct GhostRunnerConfig {
    int timeLimitMs = 0;
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

struct GhostRunnerSession {
    int score = 0;
    void reset() { score = 0; }
};

inline GhostRunnerConfig makeGhostRunnerEasyConfig() {
    GhostRunnerConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

inline GhostRunnerConfig makeGhostRunnerHardConfig() {
    GhostRunnerConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

const GhostRunnerConfig GHOST_RUNNER_EASY = makeGhostRunnerEasyConfig();
const GhostRunnerConfig GHOST_RUNNER_HARD = makeGhostRunnerHardConfig();

/*
 * Ghost Runner — stub minigame.
 *
 * Placeholder implementation: shows intro, auto-wins after 2s.
 * Phase 2 replaces with real ghost-running mechanics.
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
