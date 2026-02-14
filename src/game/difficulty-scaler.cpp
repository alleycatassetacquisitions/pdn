#include "game/difficulty-scaler.hpp"
#include <algorithm>
#include <cmath>

// PerformanceMetrics implementation

void PerformanceMetrics::recordResult(bool won, uint32_t timeMs) {
    // Add to circular buffer
    recentResults[resultIndex] = won;
    recentTimes[resultIndex] = timeMs;
    resultIndex = (resultIndex + 1) % WINDOW_SIZE;
    if (resultCount < WINDOW_SIZE) {
        resultCount++;
    }

    // Update streak
    if (won) {
        currentStreak = (currentStreak >= 0) ? currentStreak + 1 : 1;
    } else {
        currentStreak = (currentStreak <= 0) ? currentStreak - 1 : -1;
    }

    totalPlayed++;
    recalculate();
}

void PerformanceMetrics::recalculate() {
    if (resultCount == 0) {
        recentWinRate = 0.0f;
        avgCompletionTime = 0.0f;
        return;
    }

    // Calculate win rate
    int wins = 0;
    uint64_t totalTime = 0;
    for (int i = 0; i < resultCount; i++) {
        if (recentResults[i]) {
            wins++;
        }
        totalTime += recentTimes[i];
    }

    recentWinRate = static_cast<float>(wins) / static_cast<float>(resultCount);
    avgCompletionTime = static_cast<float>(totalTime) / static_cast<float>(resultCount);
}

void PerformanceMetrics::reset() {
    recentWinRate = 0.0f;
    avgCompletionTime = 0.0f;
    currentStreak = 0;
    totalPlayed = 0;
    resultIndex = 0;
    resultCount = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        recentResults[i] = false;
        recentTimes[i] = 0;
    }
}

// DifficultyScaler implementation

DifficultyScaler::DifficultyScaler() {
    // Initialize all scales to 0.0 (easiest)
    for (int i = 0; i < MAX_GAMES; i++) {
        difficultyScale[i] = 0.0f;
    }
}

float DifficultyScaler::getScaledDifficulty(GameType gameType) const {
    return getCurrentScale(gameType);
}

void DifficultyScaler::recordResult(GameType gameType, bool won, uint32_t completionTimeMs) {
    int idx = gameTypeToIndex(gameType);
    if (idx < 0 || idx >= MAX_GAMES) {
        return;
    }

    metrics[idx].recordResult(won, completionTimeMs);
    adjustScale(gameType);
}

void DifficultyScaler::reset(GameType gameType) {
    int idx = gameTypeToIndex(gameType);
    if (idx < 0 || idx >= MAX_GAMES) {
        return;
    }

    metrics[idx].reset();
    difficultyScale[idx] = 0.0f;
}

void DifficultyScaler::resetAll() {
    for (int i = 0; i < MAX_GAMES; i++) {
        metrics[i].reset();
        difficultyScale[i] = 0.0f;
    }
}

std::string DifficultyScaler::getDifficultyLabel(GameType gameType) const {
    float scale = getCurrentScale(gameType);

    if (scale < 0.15f) return "Easy";
    if (scale < 0.35f) return "Easy+";
    if (scale < 0.48f) return "Medium-";
    if (scale < 0.65f) return "Medium";
    if (scale < 0.80f) return "Medium+";
    if (scale < 0.95f) return "Hard-";
    return "Hard";
}

float DifficultyScaler::getCurrentScale(GameType gameType) const {
    int idx = gameTypeToIndex(gameType);
    if (idx < 0 || idx >= MAX_GAMES) {
        return 0.0f;
    }
    return difficultyScale[idx];
}

PerformanceMetrics DifficultyScaler::getMetrics(GameType gameType) const {
    int idx = gameTypeToIndex(gameType);
    if (idx < 0 || idx >= MAX_GAMES) {
        return PerformanceMetrics{};
    }
    return metrics[idx];
}

void DifficultyScaler::adjustScale(GameType gameType) {
    int idx = gameTypeToIndex(gameType);
    if (idx < 0 || idx >= MAX_GAMES) {
        return;
    }

    const PerformanceMetrics& m = metrics[idx];

    // Need at least 3 games to start adjusting
    if (m.totalPlayed < 3) {
        return;
    }

    float currentScale = difficultyScale[idx];
    float adjustment = 0.0f;

    // 1. Win/loss adjustment
    if (m.currentStreak > 0) {
        // Win: increase difficulty
        adjustment += SCALE_UP_STEP;

        // Streak bonus (capped at 5x)
        int streakBonus = std::min(m.currentStreak - 1, 4);
        adjustment += STREAK_BONUS * streakBonus;
    } else if (m.currentStreak < 0) {
        // Loss: decrease difficulty (faster than increase)
        adjustment -= SCALE_DOWN_STEP;

        // Streak penalty (capped at 5x)
        int streakPenalty = std::min(-m.currentStreak - 1, 4);
        adjustment -= STREAK_BONUS * streakPenalty;
    }

    // 2. Win rate correction (only if we have enough data)
    if (m.resultCount >= 5) {
        if (m.recentWinRate > 0.80f) {
            // Player is winning too much - increase difficulty
            adjustment += 0.03f;
        } else if (m.recentWinRate < 0.40f) {
            // Player is struggling - decrease difficulty
            adjustment -= 0.04f;
        }
    }

    // 3. Time-based adjustment (if player is completing very fast, nudge up)
    // Only applies if we have time data and player is winning
    if (m.resultCount >= 5 && m.recentWinRate > 0.60f && m.avgCompletionTime > 0) {
        // This is game-specific and would need calibration per game
        // For now, we'll skip this to avoid over-tuning
    }

    // Apply adjustment and clamp
    difficultyScale[idx] = clampScale(currentScale + adjustment);
}

float DifficultyScaler::clampScale(float scale) const {
    return std::max(MIN_SCALE, std::min(MAX_SCALE, scale));
}

int DifficultyScaler::gameTypeToIndex(GameType gameType) const {
    int idx = static_cast<int>(gameType);
    if (idx < 0 || idx >= MAX_GAMES) {
        return -1;
    }
    return idx;
}
