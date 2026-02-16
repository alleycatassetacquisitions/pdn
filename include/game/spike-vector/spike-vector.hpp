#pragma once

#include "game/minigame.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include <cstdlib>
#include <vector>

constexpr int SPIKE_VECTOR_APP_ID = 5;

// Speed level to ms-per-pixel mapping (1-indexed, 0 unused)
static constexpr int SPEED_TABLE[] = {
    0,    // unused (1-indexed)
    60,   // speed 1
    50,   // speed 2
    40,   // speed 3
    35,   // speed 4
    30,   // speed 5
    25,   // speed 6
    20,   // speed 7
    15,   // speed 8
};

struct SpikeVectorConfig {
    // Per-level scaling (5 levels per mode)
    int levels = 5;
    int baseWallCount = 5;          // walls on level 1
    int wallCountIncrement = 1;     // additional walls per level (0 or 1)
    int baseSpeedLevel = 1;         // speed level on level 1 (1-8)
    int speedLevelIncrement = 1;    // speed increase per level
    int baseMaxGapDistance = 2;     // max gap distance on level 1

    // Fixed parameters
    int numPositions = 5;           // lane count (5 easy, 7 hard)
    int startPosition = 2;          // player starts in middle
    int hitsAllowed = 3;            // damage before losing
    int wallWidth = 6;              // pixels
    int wallSpacing = 14;           // inter-wall gap in pixels

    unsigned long rngSeed = 0;      // 0 = random, nonzero = deterministic
    bool managedMode = false;
};

struct SpikeVectorSession {
    int cursorPosition = 2;
    int currentLevel = 0;           // 0-indexed (was currentWave)
    int hits = 0;
    int score = 0;

    // Per-level state (reset each level)
    std::vector<int> gapPositions;  // gap position for each wall
    int formationX = 128;           // current X offset of formation
    int nextWallIndex = 0;          // which wall to evaluate next
    bool levelComplete = false;

    // Visual feedback
    bool primaryPressed = false;
    bool secondaryPressed = false;

    void reset() {
        cursorPosition = 2;
        currentLevel = 0;
        hits = 0;
        score = 0;
        gapPositions.clear();
        formationX = 128;
        nextWallIndex = 0;
        levelComplete = false;
        primaryPressed = false;
        secondaryPressed = false;
    }
};

// Helper functions for level parameter derivation
inline int wallsForLevel(const SpikeVectorConfig& c, int level) {
    // Easy: 5,6,6,7,8  Hard: 8,9,10,11,12
    // Spread increment across levels with rounding
    int additionalWalls = (level * c.wallCountIncrement * 3) / (c.levels - 1);
    return c.baseWallCount + additionalWalls;
}

inline int speedMsForLevel(const SpikeVectorConfig& c, int level) {
    int speedLevel = c.baseSpeedLevel + (level * c.speedLevelIncrement);
    // Clamp to valid range [1, 8]
    if (speedLevel < 1) speedLevel = 1;
    if (speedLevel > 8) speedLevel = 8;
    return SPEED_TABLE[speedLevel];
}

inline int maxGapForLevel(const SpikeVectorConfig& c, int level) {
    // Easy: 2,2,3,3,4 ramp. Hard: 6,6,6,6,6 flat (already at max)
    if (c.baseMaxGapDistance >= c.numPositions - 1) {
        return c.numPositions - 1;
    }
    int range = (c.numPositions - 1) - c.baseMaxGapDistance;  // room to grow
    return c.baseMaxGapDistance + (level * range / (c.levels - 1));
}

inline SpikeVectorConfig makeSpikeVectorEasyConfig() {
    SpikeVectorConfig c;
    c.levels = 5;
    c.baseWallCount = 5;
    c.wallCountIncrement = 1;       // 5,6,6,7,8 walls across levels
    c.baseSpeedLevel = 1;           // speeds: 1,2,3,4,5
    c.speedLevelIncrement = 1;
    c.baseMaxGapDistance = 2;       // max gap: 2,2,3,3,4 across levels
    c.numPositions = 5;
    c.startPosition = 2;
    c.hitsAllowed = 3;
    c.wallWidth = 6;
    c.wallSpacing = 14;
    return c;
}

inline SpikeVectorConfig makeSpikeVectorHardConfig() {
    SpikeVectorConfig c;
    c.levels = 5;
    c.baseWallCount = 8;
    c.wallCountIncrement = 1;       // 8,9,10,11,12 walls
    c.baseSpeedLevel = 4;           // speeds: 4,5,6,7,8
    c.speedLevelIncrement = 1;
    c.baseMaxGapDistance = 6;       // full range always (numPositions-1)
    c.numPositions = 7;
    c.startPosition = 3;
    c.hitsAllowed = 1;
    c.wallWidth = 6;
    c.wallSpacing = 14;
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
