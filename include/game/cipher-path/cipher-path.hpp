#pragma once

#include "game/minigame.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include <cstdlib>

constexpr int CIPHER_PATH_APP_ID = 6;

struct CipherPathConfig {
    int cols = 5;                 // grid columns (5 for easy, 7 for hard)
    int rows = 4;                 // grid rows (4 for easy, 5 for hard)
    int rounds = 1;               // puzzles to solve (1 for easy, 3 for hard)
    int flowSpeedMs = 200;        // ms per pixel of flow advancement
    int flowSpeedDecayMs = 0;     // ms/pixel reduction per round (hard mode escalation)
    int noisePercent = 30;        // % of non-path cells that get noise wires
    unsigned long rngSeed = 0;    // 0 = random, nonzero = deterministic
    bool managedMode = false;
};

struct CipherPathSession {
    int currentRound = 0;
    int score = 0;

    // Grid state (max 7x5 = 35 cells)
    int tileType[35];             // 0=empty, 1=straight, 2=elbow, 3=t-junction, 4=cross, 5=endpoint
    int tileRotation[35];         // current rotation: 0, 1, 2, 3 (×90°)
    int correctRotation[35];      // solution rotation for validation
    int pathOrder[35];            // index in path sequence (-1 = not on path)
    int pathLength;               // number of tiles on the path

    // Flow state
    int flowTileIndex;            // which path tile electricity is currently in (0-based)
    int flowPixelInTile;          // pixel position within current tile (0 to ~9)
    bool flowActive;              // true when electricity is advancing

    // Cursor state
    int cursorPathIndex;          // which path tile the cursor is on (index into path sequence)

    void reset() {
        currentRound = 0;
        score = 0;
        for (int i = 0; i < 35; i++) {
            tileType[i] = 0;
            tileRotation[i] = 0;
            correctRotation[i] = 0;
            pathOrder[i] = -1;
        }
        pathLength = 0;
        flowTileIndex = 0;
        flowPixelInTile = 0;
        flowActive = false;
        cursorPathIndex = 0;
    }
};

inline CipherPathConfig makeCipherPathEasyConfig() {
    CipherPathConfig c;
    c.cols = 5;
    c.rows = 4;
    c.rounds = 1;              // Single puzzle
    c.flowSpeedMs = 200;       // ~1.8s per tile, ~20s total
    c.flowSpeedDecayMs = 0;    // N/A (1 round)
    c.noisePercent = 30;       // 30% of non-path cells get thin noise wires
    return c;
}

inline CipherPathConfig makeCipherPathHardConfig() {
    CipherPathConfig c;
    c.cols = 7;
    c.rows = 5;
    c.rounds = 3;              // Three puzzles, escalating speed
    c.flowSpeedMs = 80;        // ~0.7s per tile, ~15s total
    c.flowSpeedDecayMs = 10;   // Round 2: 70ms, Round 3: 60ms
    c.noisePercent = 40;       // 40% noise — busier board
    return c;
}

const CipherPathConfig CIPHER_PATH_EASY = makeCipherPathEasyConfig();
const CipherPathConfig CIPHER_PATH_HARD = makeCipherPathHardConfig();

/*
 * Cipher Path -- wire routing puzzle minigame.
 *
 * BioShock-inspired wire rotation puzzle with real-time electricity flow.
 * Player rotates scrambled wire tiles to reconnect a circuit before
 * electricity reaches a dead end. 2D grid, input/output terminals fixed,
 * flow advances pixel-by-pixel creating time pressure.
 *
 * In managed mode (via FDN), terminal states call
 * Device::returnToPreviousApp(). In standalone mode, loops to intro.
 */
class CipherPath : public MiniGame {
public:
    explicit CipherPath(CipherPathConfig config) :
        MiniGame(CIPHER_PATH_APP_ID, GameType::CIPHER_PATH, "CIPHER PATH"),
        config(config)
    {
    }

    void populateStateMap() override;
    void resetGame() override;

    CipherPathConfig& getConfig() { return config; }
    CipherPathSession& getSession() { return session; }

    void generatePath();    // generates the wire path and scrambles tiles
    void generateNoise();   // adds noise tiles to non-path cells

private:
    CipherPathConfig config;
    CipherPathSession session;
};
