#pragma once

#include "game/minigame.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include <cstdlib>
#include <vector>
#include <cstdint>

constexpr int GHOST_RUNNER_APP_ID = 4;

enum class NoteType : uint8_t {
    PRESS = 0,
    HOLD = 1
};

enum class Lane : uint8_t {
    UP = 0,
    DOWN = 1
};

enum class NoteGrade : uint8_t {
    NONE = 0,
    MISS = 1,
    GOOD = 2,
    PERFECT = 3
};

struct Note {
    NoteType type = NoteType::PRESS;
    Lane lane = Lane::UP;
    int xPosition = 128;          // start position (offscreen right)
    int holdLength = 0;           // length in pixels (for HOLD notes)
    NoteGrade grade = NoteGrade::NONE;
    bool active = true;           // still needs to be processed
};

struct GhostRunnerConfig {
    // Rhythm game parameters
    int rounds = 4;
    int notesPerRound = 8;
    float dualLaneChance = 0.0f;      // probability of dual-lane notes
    float holdNoteChance = 0.15f;     // probability of hold notes
    int ghostSpeedMs = 50;            // ms per pixel movement
    float speedRampPerRound = 1.0f;   // speed multiplier per round
    int holdDurationMs = 800;         // hold note duration
    int hitZoneWidthPx = 20;          // hit zone width
    int perfectZonePx = 6;            // perfect zone width (centered in hit zone)
    int lives = 3;                    // lives before losing

    // Display layout constants
    static constexpr int HIT_ZONE_X = 8;
    static constexpr int UP_LANE_Y = 10;
    static constexpr int UP_LANE_HEIGHT = 20;
    static constexpr int DIVIDER_Y = 31;
    static constexpr int DOWN_LANE_Y = 33;
    static constexpr int DOWN_LANE_HEIGHT = 20;

    // Legacy fields (for backward compatibility, may be removed)
    int screenWidth = 128;
    int targetZoneStart = 40;
    int targetZoneEnd = 60;
    int missesAllowed = 2;

    unsigned long rngSeed = 0;        // 0 = random, nonzero = deterministic
    bool managedMode = false;
};

struct GhostRunnerSession {
    // Round tracking
    int currentRound = 0;
    int score = 0;
    int livesRemaining = 3;

    // Pattern and note tracking
    std::vector<Note> currentPattern;
    int currentNoteIndex = 0;

    // Input tracking
    bool upPressed = false;
    bool downPressed = false;

    // Hold state tracking
    bool upHoldActive = false;
    bool downHoldActive = false;
    int upHoldNoteIndex = -1;
    int downHoldNoteIndex = -1;

    // Statistics
    int perfectCount = 0;
    int goodCount = 0;
    int missCount = 0;

    // Legacy fields (for backward compatibility)
    int ghostPosition = 0;
    int strikes = 0;
    bool playerPressed = false;

    void reset() {
        currentRound = 0;
        score = 0;
        livesRemaining = 3;
        currentPattern.clear();
        currentNoteIndex = 0;
        upPressed = false;
        downPressed = false;
        upHoldActive = false;
        downHoldActive = false;
        upHoldNoteIndex = -1;
        downHoldNoteIndex = -1;
        perfectCount = 0;
        goodCount = 0;
        missCount = 0;
        ghostPosition = 0;
        strikes = 0;
        playerPressed = false;
    }
};

inline GhostRunnerConfig makeGhostRunnerEasyConfig() {
    GhostRunnerConfig c;
    c.rounds = 4;
    c.notesPerRound = 8;
    c.dualLaneChance = 0.0f;      // no dual-lane notes in easy mode
    c.holdNoteChance = 0.15f;
    c.ghostSpeedMs = 50;
    c.speedRampPerRound = 1.0f;   // no speed increase
    c.holdDurationMs = 800;
    c.hitZoneWidthPx = 20;
    c.perfectZonePx = 6;
    c.lives = 3;
    // Legacy
    c.screenWidth = 128;
    c.targetZoneStart = 35;
    c.targetZoneEnd = 65;
    c.missesAllowed = 3;
    return c;
}

inline GhostRunnerConfig makeGhostRunnerHardConfig() {
    GhostRunnerConfig c;
    c.rounds = 4;
    c.notesPerRound = 12;
    c.dualLaneChance = 0.4f;      // 40% chance of dual-lane notes
    c.holdNoteChance = 0.35f;
    c.ghostSpeedMs = 30;
    c.speedRampPerRound = 1.1f;   // 10% speed increase per round
    c.holdDurationMs = 500;
    c.hitZoneWidthPx = 14;
    c.perfectZonePx = 3;
    c.lives = 3;
    // Legacy
    c.screenWidth = 128;
    c.targetZoneStart = 42;
    c.targetZoneEnd = 58;
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

    // Pattern generation
    std::vector<Note> generatePattern(int round);

private:
    GhostRunnerConfig config;
    GhostRunnerSession session;
};
