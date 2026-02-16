#pragma once

#include "game/minigame.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include <cstdlib>
#include <vector>
#include <cstdint>

constexpr int GHOST_RUNNER_APP_ID = 4;

// Direction constants for maze navigation
constexpr int DIR_UP = 0;
constexpr int DIR_RIGHT = 1;
constexpr int DIR_DOWN = 2;
constexpr int DIR_LEFT = 3;

// Wall bitmasks (4 bits per cell)
constexpr uint8_t WALL_UP = 0x01;
constexpr uint8_t WALL_RIGHT = 0x02;
constexpr uint8_t WALL_DOWN = 0x04;
constexpr uint8_t WALL_LEFT = 0x08;

struct GhostRunnerConfig {
    // Maze dimensions
    int cols = 5;                  // maze width in cells
    int rows = 3;                  // maze height in cells
    int rounds = 4;                // rounds to complete
    int lives = 3;                 // wall bonks before game over

    // Timing parameters (milliseconds)
    int previewMazeMs = 4000;      // maze visible duration
    int previewTraceMs = 4000;     // solution trace duration
    int bonkFlashMs = 1000;        // maze flash duration on bonk

    // Start/exit positions
    int startRow = 0;              // start cell row
    int startCol = 0;              // start cell col
    int exitRow = 2;               // exit cell row (rows-1)
    int exitCol = 4;               // exit cell col (cols-1)

    // Difficulty scaling
    float previewShrinkPerRound = 0.85f;  // preview time multiplier per round

    unsigned long rngSeed = 0;        // 0 = random, nonzero = deterministic
    bool managedMode = false;
};

struct GhostRunnerSession {
    // Player state
    int cursorRow = 0;
    int cursorCol = 0;
    int currentDirection = DIR_RIGHT;  // start facing RIGHT
    int stepsUsed = 0;
    int currentRound = 0;
    int livesRemaining = 3;
    int score = 0;
    int bonkCount = 0;             // bonks in current round

    // Maze data (generated per round)
    // Wall storage: bitfield per cell, 4 bits = UP/RIGHT/DOWN/LEFT walls
    uint8_t walls[7 * 5];          // max grid size 7×5, 4 bits per cell
    int solutionPath[50];          // direction sequence (max ~35 moves for 7×5)
    int solutionLength = 0;

    // Visual state
    bool mazeFlashActive = false;

    void reset() {
        cursorRow = 0;
        cursorCol = 0;
        currentDirection = DIR_RIGHT;
        stepsUsed = 0;
        currentRound = 0;
        livesRemaining = 3;
        score = 0;
        bonkCount = 0;
        solutionLength = 0;
        mazeFlashActive = false;
        for (int i = 0; i < 7 * 5; i++) {
            walls[i] = 0;
        }
        for (int i = 0; i < 50; i++) {
            solutionPath[i] = 0;
        }
    }
};

inline GhostRunnerConfig makeGhostRunnerEasyConfig() {
    GhostRunnerConfig c;
    c.cols = 5;
    c.rows = 3;
    c.rounds = 4;
    c.lives = 3;
    c.previewMazeMs = 4000;
    c.previewTraceMs = 4000;
    c.bonkFlashMs = 1000;
    c.startRow = 0;
    c.startCol = 0;
    c.exitRow = 2;
    c.exitCol = 4;
    c.previewShrinkPerRound = 0.85f;
    return c;
}

inline GhostRunnerConfig makeGhostRunnerHardConfig() {
    GhostRunnerConfig c;
    c.cols = 7;
    c.rows = 5;
    c.rounds = 6;
    c.lives = 1;
    c.previewMazeMs = 2500;
    c.previewTraceMs = 3000;
    c.bonkFlashMs = 500;
    c.startRow = 0;
    c.startCol = 0;
    c.exitRow = 4;
    c.exitCol = 6;
    c.previewShrinkPerRound = 0.75f;
    return c;
}

const GhostRunnerConfig GHOST_RUNNER_EASY = makeGhostRunnerEasyConfig();
const GhostRunnerConfig GHOST_RUNNER_HARD = makeGhostRunnerHardConfig();

/*
 * Ghost Runner — memory maze minigame.
 *
 * A maze is briefly shown during preview phase, then becomes invisible.
 * Player navigates from memory using:
 *   PRIMARY button: Cycle direction (UP → RIGHT → DOWN → LEFT)
 *   SECONDARY button: Move in current direction
 *
 * Bonking into invisible walls costs a life but flashes the maze visible.
 * Complete all rounds to win, lose all lives to lose.
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

    // Maze generation and solution finding
    void generateMaze(int round);
    void findSolution();

private:
    GhostRunnerConfig config;
    GhostRunnerSession session;
};
