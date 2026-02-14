#pragma once

#include <cstdint>
#include <string>
#include "device/device-types.hpp"

/*
 * Performance tracking for a single game.
 * Tracks win rate, completion time, and streaks across a sliding window
 * of recent attempts to enable dynamic difficulty adjustment.
 */
struct PerformanceMetrics {
    float recentWinRate = 0.0f;      // win rate over last WINDOW_SIZE games
    float avgCompletionTime = 0.0f;  // average time in ms over last WINDOW_SIZE games
    int currentStreak = 0;           // positive = wins, negative = losses
    int totalPlayed = 0;             // total games played

    // Circular buffer for recent results
    static const int WINDOW_SIZE = 10;
    bool recentResults[WINDOW_SIZE] = {};     // true = win
    uint32_t recentTimes[WINDOW_SIZE] = {};   // completion times in ms
    int resultIndex = 0;
    int resultCount = 0;

    /*
     * Record a game result and update the circular buffer.
     * Automatically recalculates metrics.
     */
    void recordResult(bool won, uint32_t timeMs);

    /*
     * Recalculate win rate and average completion time from buffer.
     */
    void recalculate();

    /*
     * Reset all metrics to initial state.
     */
    void reset();
};

/*
 * Dynamic difficulty scaler for all minigames.
 *
 * Tracks player performance (win rate, completion time, streaks) and
 * automatically adjusts difficulty parameters within the easy/hard bounds
 * to maintain engagement. Targets ~65% win rate.
 *
 * Usage:
 * 1. Record results after each game: recordResult(gameId, won, timeMs)
 * 2. Get scaled difficulty: getScaledDifficulty(gameId) returns 0.0-1.0
 * 3. Interpolate game parameters: lerp(easyValue, hardValue, scale)
 *
 * The scaler respects player's difficulty selection:
 * - Easy mode: scale range is 0.0-0.5 (within easy parameters)
 * - Hard mode: scale range is 0.5-1.0 (within hard parameters)
 */
class DifficultyScaler {
public:
    DifficultyScaler();

    /*
     * Get the current scaled difficulty for a game (0.0 = easiest, 1.0 = hardest).
     * This float is used to interpolate between easy and hard parameter bounds.
     */
    float getScaledDifficulty(GameType gameType) const;

    /*
     * Record a game result and potentially adjust difficulty.
     * won: true if player won the game
     * completionTimeMs: time taken to complete (or lose) the game in milliseconds
     */
    void recordResult(GameType gameType, bool won, uint32_t completionTimeMs);

    /*
     * Reset scaling for a specific game (or all games).
     */
    void reset(GameType gameType);
    void resetAll();

    /*
     * Get human-readable difficulty description.
     * Examples: "Easy", "Easy+", "Medium-", "Medium", "Medium+", "Hard-", "Hard"
     */
    std::string getDifficultyLabel(GameType gameType) const;

    /*
     * Get current scaling parameters for display/debug.
     */
    float getCurrentScale(GameType gameType) const;
    PerformanceMetrics getMetrics(GameType gameType) const;

private:
    static const int MAX_GAMES = 8;  // QUICKDRAW + 7 minigames
    PerformanceMetrics metrics[MAX_GAMES];
    float difficultyScale[MAX_GAMES] = {};  // 0.0 to 1.0

    // Scaling parameters
    static constexpr float SCALE_UP_STEP = 0.05f;      // increase per win
    static constexpr float SCALE_DOWN_STEP = 0.08f;    // decrease per loss (faster)
    static constexpr float WIN_RATE_TARGET = 0.65f;    // target win rate
    static constexpr float STREAK_BONUS = 0.02f;       // extra scaling per streak game
    static constexpr float MIN_SCALE = 0.0f;
    static constexpr float MAX_SCALE = 1.0f;

    /*
     * Adjust the difficulty scale based on current performance.
     * Called automatically after recordResult().
     */
    void adjustScale(GameType gameType);

    /*
     * Helper to clamp scale to valid range.
     */
    float clampScale(float scale) const;

    /*
     * Convert GameType to array index (0-7).
     */
    int gameTypeToIndex(GameType gameType) const;
};
