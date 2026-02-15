#pragma once

#include "game/minigame.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include <cstdlib>

constexpr int SPIKE_VECTOR_APP_ID = 5;

struct SpikeVectorConfig {
    int approachSpeedMs = 40;     // ms per wall step (lower = faster)
    int trackLength = 100;        // wall travels 0 to trackLength
    int numPositions = 5;         // number of lane positions (0 to numPositions-1)
    int startPosition = 2;        // player starts in middle
    int waves = 5;                // total waves to survive
    int hitsAllowed = 2;          // damage before losing
    unsigned long rngSeed = 0;    // 0 = random, nonzero = deterministic
    bool managedMode = false;
};

struct SpikeVectorSession {
    int cursorPosition = 2;       // current player position
    int wallPosition = 0;         // current wall distance (0 = far, trackLength = arrived)
    int gapPosition = 0;          // where the gap is this wave
    int currentWave = 0;
    int hits = 0;
    int score = 0;
    bool wallArrived = false;     // true when wall reaches end
    void reset() {
        cursorPosition = 2;
        wallPosition = 0;
        gapPosition = 0;
        currentWave = 0;
        hits = 0;
        score = 0;
        wallArrived = false;
    }
};

inline SpikeVectorConfig makeSpikeVectorEasyConfig() {
    SpikeVectorConfig c;
    c.approachSpeedMs = 40;
    c.trackLength = 100;
    c.numPositions = 5;
    c.startPosition = 2;
    c.waves = 5;
    c.hitsAllowed = 3;    // generous
    return c;
}

inline SpikeVectorConfig makeSpikeVectorHardConfig() {
    SpikeVectorConfig c;
    c.approachSpeedMs = 20;       // faster walls
    c.trackLength = 100;
    c.numPositions = 7;           // more positions to track
    c.startPosition = 3;
    c.waves = 8;                  // more waves
    c.hitsAllowed = 1;            // almost no margin
    return c;
}

const SpikeVectorConfig SPIKE_VECTOR_EASY = makeSpikeVectorEasyConfig();
const SpikeVectorConfig SPIKE_VECTOR_HARD = makeSpikeVectorHardConfig();

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
