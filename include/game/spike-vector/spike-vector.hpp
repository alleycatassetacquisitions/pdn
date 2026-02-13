#pragma once

#include "game/minigame.hpp"
#include "game/spike-vector/spike-vector-states.hpp"

constexpr int SPIKE_VECTOR_APP_ID = 5;

struct SpikeVectorConfig {
    int timeLimitMs = 0;
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

struct SpikeVectorSession {
    int score = 0;
    void reset() { score = 0; }
};

inline SpikeVectorConfig makeSpikeVectorEasyConfig() {
    SpikeVectorConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

inline SpikeVectorConfig makeSpikeVectorHardConfig() {
    SpikeVectorConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

const SpikeVectorConfig SPIKE_VECTOR_EASY = makeSpikeVectorEasyConfig();
const SpikeVectorConfig SPIKE_VECTOR_HARD = makeSpikeVectorHardConfig();

/*
 * Spike Vector — stub minigame.
 *
 * Placeholder implementation: shows intro, auto-wins after 2s.
 * Phase 2 replaces with real spike-dodging mechanics.
 *
 * In managed mode (via FDN), terminal states call
 * Device::returnToPreviousApp(). In standalone mode, loops to intro.
 */
class SpikeVector : public MiniGame {
public:
    explicit SpikeVector(SpikeVectorConfig config) :
        MiniGame(SPIKE_VECTOR_APP_ID, GameType::SPIKE_VECTOR, "SPIKE VECTOR"),
        config(config)
    {
    }

    void populateStateMap() override;
    void resetGame() override;

    SpikeVectorConfig& getConfig() { return config; }
    SpikeVectorSession& getSession() { return session; }

private:
    SpikeVectorConfig config;
    SpikeVectorSession session;
};
