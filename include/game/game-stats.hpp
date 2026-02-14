#pragma once

#include <cstdint>
#include <cstddef>
#include "device/device-types.hpp"

/*
 * Game statistics tracking for FDN minigames.
 * Tracks wins, losses, completion times, and streaks per game per difficulty.
 */

constexpr size_t NUM_TRACKED_GAMES = 7;  // GHOST_RUNNER through SIGNAL_ECHO

struct GameStats {
    uint16_t wins = 0;
    uint16_t losses = 0;
    uint16_t totalAttempts = 0;
    uint32_t bestTimeMs = UINT32_MAX;  // fastest completion in milliseconds
    uint32_t totalTimeMs = 0;          // for computing average
    uint16_t currentWinStreak = 0;
    uint16_t bestWinStreak = 0;
    uint16_t currentLossStreak = 0;

    void reset() {
        wins = 0;
        losses = 0;
        totalAttempts = 0;
        bestTimeMs = UINT32_MAX;
        totalTimeMs = 0;
        currentWinStreak = 0;
        bestWinStreak = 0;
        currentLossStreak = 0;
    }
};

struct PerGameStats {
    GameStats easy;
    GameStats hard;
    GameStats combined;  // aggregated across difficulties

    void reset() {
        easy.reset();
        hard.reset();
        combined.reset();
    }
};

/*
 * Statistics tracker for all minigames.
 * Provides recording and querying methods for game outcomes.
 */
class GameStatsTracker {
public:
    GameStatsTracker() {
        for (size_t i = 0; i < NUM_TRACKED_GAMES; i++) {
            gameStats[i] = PerGameStats{};
        }
    }

    /*
     * Record a win for the given game and difficulty.
     * timeMs: completion time in milliseconds
     */
    void recordWin(GameType gameType, bool hardMode, uint32_t timeMs) {
        int idx = getGameIndex(gameType);
        if (idx < 0) return;

        PerGameStats& stats = gameStats[idx];
        GameStats& diffStats = hardMode ? stats.hard : stats.easy;
        GameStats& combinedStats = stats.combined;

        // Update counts
        diffStats.wins++;
        diffStats.totalAttempts++;
        combinedStats.wins++;
        combinedStats.totalAttempts++;

        // Update time tracking
        if (timeMs < diffStats.bestTimeMs) {
            diffStats.bestTimeMs = timeMs;
        }
        diffStats.totalTimeMs += timeMs;

        if (timeMs < combinedStats.bestTimeMs) {
            combinedStats.bestTimeMs = timeMs;
        }
        combinedStats.totalTimeMs += timeMs;

        // Update streaks
        diffStats.currentWinStreak++;
        diffStats.currentLossStreak = 0;
        if (diffStats.currentWinStreak > diffStats.bestWinStreak) {
            diffStats.bestWinStreak = diffStats.currentWinStreak;
        }

        combinedStats.currentWinStreak++;
        combinedStats.currentLossStreak = 0;
        if (combinedStats.currentWinStreak > combinedStats.bestWinStreak) {
            combinedStats.bestWinStreak = combinedStats.currentWinStreak;
        }
    }

    /*
     * Record a loss for the given game and difficulty.
     * timeMs: attempt duration in milliseconds
     */
    void recordLoss(GameType gameType, bool hardMode, uint32_t timeMs) {
        int idx = getGameIndex(gameType);
        if (idx < 0) return;

        PerGameStats& stats = gameStats[idx];
        GameStats& diffStats = hardMode ? stats.hard : stats.easy;
        GameStats& combinedStats = stats.combined;

        // Update counts
        diffStats.losses++;
        diffStats.totalAttempts++;
        combinedStats.losses++;
        combinedStats.totalAttempts++;

        // Update time tracking (for losses too, to track average attempt time)
        diffStats.totalTimeMs += timeMs;
        combinedStats.totalTimeMs += timeMs;

        // Update streaks
        diffStats.currentWinStreak = 0;
        diffStats.currentLossStreak++;

        combinedStats.currentWinStreak = 0;
        combinedStats.currentLossStreak++;
    }

    /*
     * Get win rate (0.0 to 1.0) for the given game and difficulty.
     * Returns 0.0 if no attempts have been made.
     */
    float getWinRate(GameType gameType, bool hardMode) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return 0.0f;

        const PerGameStats& stats = gameStats[idx];
        const GameStats& diffStats = hardMode ? stats.hard : stats.easy;

        if (diffStats.totalAttempts == 0) return 0.0f;
        return static_cast<float>(diffStats.wins) / static_cast<float>(diffStats.totalAttempts);
    }

    /*
     * Get combined win rate across all difficulties.
     */
    float getCombinedWinRate(GameType gameType) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return 0.0f;

        const GameStats& combinedStats = gameStats[idx].combined;
        if (combinedStats.totalAttempts == 0) return 0.0f;
        return static_cast<float>(combinedStats.wins) / static_cast<float>(combinedStats.totalAttempts);
    }

    /*
     * Get average completion time in milliseconds.
     * Returns 0 if no attempts have been made.
     */
    uint32_t getAverageTime(GameType gameType, bool hardMode) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return 0;

        const PerGameStats& stats = gameStats[idx];
        const GameStats& diffStats = hardMode ? stats.hard : stats.easy;

        if (diffStats.totalAttempts == 0) return 0;
        return diffStats.totalTimeMs / diffStats.totalAttempts;
    }

    /*
     * Get combined average time across all difficulties.
     */
    uint32_t getCombinedAverageTime(GameType gameType) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return 0;

        const GameStats& combinedStats = gameStats[idx].combined;
        if (combinedStats.totalAttempts == 0) return 0;
        return combinedStats.totalTimeMs / combinedStats.totalAttempts;
    }

    /*
     * Get best completion time in milliseconds.
     * Returns UINT32_MAX if no wins have been recorded.
     */
    uint32_t getBestTime(GameType gameType, bool hardMode) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return UINT32_MAX;

        const PerGameStats& stats = gameStats[idx];
        const GameStats& diffStats = hardMode ? stats.hard : stats.easy;
        return diffStats.bestTimeMs;
    }

    /*
     * Get combined best time across all difficulties.
     */
    uint32_t getCombinedBestTime(GameType gameType) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return UINT32_MAX;

        return gameStats[idx].combined.bestTimeMs;
    }

    /*
     * Get stats for a specific game and difficulty.
     */
    const GameStats* getStats(GameType gameType, bool hardMode) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return nullptr;

        const PerGameStats& stats = gameStats[idx];
        return hardMode ? &stats.hard : &stats.easy;
    }

    /*
     * Get combined stats for a specific game.
     */
    const GameStats* getCombinedStats(GameType gameType) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return nullptr;

        return &gameStats[idx].combined;
    }

    /*
     * Get per-game stats for all difficulties.
     */
    const PerGameStats* getPerGameStats(GameType gameType) const {
        int idx = getGameIndex(gameType);
        if (idx < 0) return nullptr;

        return &gameStats[idx];
    }

    /*
     * Reset all statistics for a specific game.
     */
    void resetGame(GameType gameType) {
        int idx = getGameIndex(gameType);
        if (idx < 0) return;

        gameStats[idx].reset();
    }

    /*
     * Reset all statistics for all games.
     */
    void resetAll() {
        for (size_t i = 0; i < NUM_TRACKED_GAMES; i++) {
            gameStats[i].reset();
        }
    }

    /*
     * Check if any games have been played.
     */
    bool hasAnyGamesPlayed() const {
        for (size_t i = 0; i < NUM_TRACKED_GAMES; i++) {
            if (gameStats[i].combined.totalAttempts > 0) {
                return true;
            }
        }
        return false;
    }

    /*
     * Get overall statistics across all games.
     */
    GameStats getOverallStats() const {
        GameStats overall;
        for (size_t i = 0; i < NUM_TRACKED_GAMES; i++) {
            const GameStats& combined = gameStats[i].combined;
            overall.wins += combined.wins;
            overall.losses += combined.losses;
            overall.totalAttempts += combined.totalAttempts;
            overall.totalTimeMs += combined.totalTimeMs;

            if (combined.bestTimeMs < overall.bestTimeMs) {
                overall.bestTimeMs = combined.bestTimeMs;
            }

            // For overall streaks, we'd need to track play order
            // For now, just take the max current streak
            if (combined.currentWinStreak > overall.currentWinStreak) {
                overall.currentWinStreak = combined.currentWinStreak;
            }
            if (combined.bestWinStreak > overall.bestWinStreak) {
                overall.bestWinStreak = combined.bestWinStreak;
            }
        }
        return overall;
    }

private:
    PerGameStats gameStats[NUM_TRACKED_GAMES];

    /*
     * Map GameType to internal array index.
     * Returns -1 if game type is not tracked (e.g., QUICKDRAW).
     */
    int getGameIndex(GameType gameType) const {
        int typeValue = static_cast<int>(gameType);
        // QUICKDRAW = 0 (not tracked)
        // GHOST_RUNNER = 1, SPIKE_VECTOR = 2, ..., SIGNAL_ECHO = 7
        if (typeValue >= 1 && typeValue <= 7) {
            return typeValue - 1;  // Map to 0-6 array indices
        }
        return -1;
    }
};
