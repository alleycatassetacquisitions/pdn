#pragma once
#include <gtest/gtest.h>
#include "game/difficulty-scaler.hpp"
#include "device/device-types.hpp"

/*
 * DifficultyScaler Tests
 *
 * Tests for DifficultyScaler and PerformanceMetrics covering:
 * - Performance metrics calculation (win rate, avg time, streaks)
 * - Difficulty scaling based on performance
 * - Circular buffer behavior for recent results
 * - Reset and boundary conditions
 * - Multi-game independent scaling
 */

// ============================================
// PERFORMANCE METRICS TESTS
// ============================================

TEST(PerformanceMetricsTests, InitialStateIsZero) {
    PerformanceMetrics metrics;

    EXPECT_FLOAT_EQ(metrics.recentWinRate, 0.0f);
    EXPECT_FLOAT_EQ(metrics.avgCompletionTime, 0.0f);
    EXPECT_EQ(metrics.currentStreak, 0);
    EXPECT_EQ(metrics.totalPlayed, 0);
    EXPECT_EQ(metrics.resultCount, 0);
}

TEST(PerformanceMetricsTests, SingleWinUpdatesMetrics) {
    PerformanceMetrics metrics;
    metrics.recordResult(true, 1000);

    EXPECT_FLOAT_EQ(metrics.recentWinRate, 1.0f);
    EXPECT_FLOAT_EQ(metrics.avgCompletionTime, 1000.0f);
    EXPECT_EQ(metrics.currentStreak, 1);
    EXPECT_EQ(metrics.totalPlayed, 1);
    EXPECT_EQ(metrics.resultCount, 1);
}

TEST(PerformanceMetricsTests, SingleLossUpdatesMetrics) {
    PerformanceMetrics metrics;
    metrics.recordResult(false, 2000);

    EXPECT_FLOAT_EQ(metrics.recentWinRate, 0.0f);
    EXPECT_FLOAT_EQ(metrics.avgCompletionTime, 2000.0f);
    EXPECT_EQ(metrics.currentStreak, -1);
    EXPECT_EQ(metrics.totalPlayed, 1);
}

TEST(PerformanceMetricsTests, WinStreakIncrements) {
    PerformanceMetrics metrics;

    metrics.recordResult(true, 1000);
    EXPECT_EQ(metrics.currentStreak, 1);

    metrics.recordResult(true, 1000);
    EXPECT_EQ(metrics.currentStreak, 2);

    metrics.recordResult(true, 1000);
    EXPECT_EQ(metrics.currentStreak, 3);
}

TEST(PerformanceMetricsTests, LossStreakDecrements) {
    PerformanceMetrics metrics;

    metrics.recordResult(false, 1000);
    EXPECT_EQ(metrics.currentStreak, -1);

    metrics.recordResult(false, 1000);
    EXPECT_EQ(metrics.currentStreak, -2);

    metrics.recordResult(false, 1000);
    EXPECT_EQ(metrics.currentStreak, -3);
}

TEST(PerformanceMetricsTests, StreakResetsOnOppositeResult) {
    PerformanceMetrics metrics;

    // Win streak
    metrics.recordResult(true, 1000);
    metrics.recordResult(true, 1000);
    EXPECT_EQ(metrics.currentStreak, 2);

    // Loss resets to -1
    metrics.recordResult(false, 1000);
    EXPECT_EQ(metrics.currentStreak, -1);

    // Win resets to +1
    metrics.recordResult(true, 1000);
    EXPECT_EQ(metrics.currentStreak, 1);
}

TEST(PerformanceMetricsTests, WinRateCalculation) {
    PerformanceMetrics metrics;

    // 3 wins, 2 losses = 60% win rate
    metrics.recordResult(true, 1000);
    metrics.recordResult(true, 1000);
    metrics.recordResult(false, 1000);
    metrics.recordResult(true, 1000);
    metrics.recordResult(false, 1000);

    EXPECT_FLOAT_EQ(metrics.recentWinRate, 0.6f);
}

TEST(PerformanceMetricsTests, AverageCompletionTime) {
    PerformanceMetrics metrics;

    metrics.recordResult(true, 1000);
    metrics.recordResult(true, 2000);
    metrics.recordResult(true, 3000);

    // Average: (1000 + 2000 + 3000) / 3 = 2000
    EXPECT_FLOAT_EQ(metrics.avgCompletionTime, 2000.0f);
}

TEST(PerformanceMetricsTests, CircularBufferWrapsAround) {
    PerformanceMetrics metrics;
    const int WINDOW_SIZE = 10;  // From PerformanceMetrics::WINDOW_SIZE

    // Fill buffer (10 entries)
    for (int i = 0; i < WINDOW_SIZE; i++) {
        metrics.recordResult(true, 1000);
    }

    EXPECT_EQ(metrics.resultCount, WINDOW_SIZE);
    EXPECT_FLOAT_EQ(metrics.recentWinRate, 1.0f);

    // Add more entries - should overwrite oldest
    for (int i = 0; i < 5; i++) {
        metrics.recordResult(false, 2000);
    }

    // Still only WINDOW_SIZE entries, but win rate should change
    EXPECT_EQ(metrics.resultCount, WINDOW_SIZE);
    EXPECT_FLOAT_EQ(metrics.recentWinRate, 0.5f);  // 5 wins, 5 losses in window
}

TEST(PerformanceMetricsTests, ResetClearsAllData) {
    PerformanceMetrics metrics;

    metrics.recordResult(true, 1000);
    metrics.recordResult(true, 2000);
    metrics.recordResult(false, 1500);

    metrics.reset();

    EXPECT_FLOAT_EQ(metrics.recentWinRate, 0.0f);
    EXPECT_FLOAT_EQ(metrics.avgCompletionTime, 0.0f);
    EXPECT_EQ(metrics.currentStreak, 0);
    EXPECT_EQ(metrics.totalPlayed, 0);
    EXPECT_EQ(metrics.resultCount, 0);
    EXPECT_EQ(metrics.resultIndex, 0);
}

// ============================================
// DIFFICULTY SCALER BASIC TESTS
// ============================================

TEST(DifficultyScalerTests, InitialScaleIsZero) {
    DifficultyScaler scaler;

    EXPECT_FLOAT_EQ(scaler.getScaledDifficulty(GameType::SIGNAL_ECHO), 0.0f);
    EXPECT_FLOAT_EQ(scaler.getScaledDifficulty(GameType::GHOST_RUNNER), 0.0f);
}

TEST(DifficultyScalerTests, ScaleStaysBetweenZeroAndOne) {
    DifficultyScaler scaler;

    // Record many wins (should push scale up)
    for (int i = 0; i < 50; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    }

    float scale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    EXPECT_GE(scale, 0.0f);
    EXPECT_LE(scale, 1.0f);

    // Record many losses (should push scale down)
    for (int i = 0; i < 50; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, false, 1000);
    }

    scale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    EXPECT_GE(scale, 0.0f);
    EXPECT_LE(scale, 1.0f);
}

TEST(DifficultyScalerTests, WinsIncreaseDifficulty) {
    DifficultyScaler scaler;

    float initialScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    // Record enough wins to trigger adjustment (needs 3+ games)
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);

    float newScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    EXPECT_GT(newScale, initialScale);
}

TEST(DifficultyScalerTests, LossesDecreaseDifficulty) {
    DifficultyScaler scaler;

    // Start at higher difficulty
    for (int i = 0; i < 5; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    }

    float highScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    // Record losses
    for (int i = 0; i < 5; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, false, 1000);
    }

    float newScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    EXPECT_LT(newScale, highScale);
}

TEST(DifficultyScalerTests, NoAdjustmentWithLessThanThreeGames) {
    DifficultyScaler scaler;

    float initialScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    // Only 2 games - should not adjust
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);

    float newScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    EXPECT_FLOAT_EQ(newScale, initialScale);
}

// ============================================
// DIFFICULTY LABELS TESTS
// ============================================

TEST(DifficultyScalerTests, DifficultyLabelsCorrect) {
    DifficultyScaler scaler;

    // Manually set scales and check labels
    for (int i = 0; i < 100; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    }

    // Should be at high difficulty now
    std::string label = scaler.getDifficultyLabel(GameType::SIGNAL_ECHO);
    EXPECT_TRUE(label == "Hard" || label == "Hard-" || label == "Medium+");
}

TEST(DifficultyScalerTests, EasyLabelAtLowScale) {
    DifficultyScaler scaler;
    // Default scale is 0.0
    EXPECT_EQ(scaler.getDifficultyLabel(GameType::SIGNAL_ECHO), "Easy");
}

// ============================================
// MULTI-GAME INDEPENDENCE TESTS
// ============================================

TEST(DifficultyScalerTests, DifferentGamesScaleIndependently) {
    DifficultyScaler scaler;

    // Signal Echo: many wins
    for (int i = 0; i < 10; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    }

    // Ghost Runner: many losses
    for (int i = 0; i < 10; i++) {
        scaler.recordResult(GameType::GHOST_RUNNER, false, 1000);
    }

    float signalScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    float ghostScale = scaler.getScaledDifficulty(GameType::GHOST_RUNNER);

    // Signal Echo should be harder (higher scale)
    EXPECT_GT(signalScale, ghostScale);
}

TEST(DifficultyScalerTests, ResetSingleGameDoesNotAffectOthers) {
    DifficultyScaler scaler;

    // Build up difficulty in both games
    for (int i = 0; i < 10; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
        scaler.recordResult(GameType::GHOST_RUNNER, true, 1000);
    }

    float signalScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    float ghostScale = scaler.getScaledDifficulty(GameType::GHOST_RUNNER);

    EXPECT_GT(signalScale, 0.0f);
    EXPECT_GT(ghostScale, 0.0f);

    // Reset only Signal Echo
    scaler.reset(GameType::SIGNAL_ECHO);

    EXPECT_FLOAT_EQ(scaler.getScaledDifficulty(GameType::SIGNAL_ECHO), 0.0f);
    EXPECT_FLOAT_EQ(scaler.getScaledDifficulty(GameType::GHOST_RUNNER), ghostScale);
}

TEST(DifficultyScalerTests, ResetAllClearsAllGames) {
    DifficultyScaler scaler;

    // Build up difficulty in multiple games
    for (int i = 0; i < 10; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
        scaler.recordResult(GameType::GHOST_RUNNER, true, 1000);
        scaler.recordResult(GameType::CIPHER_PATH, true, 1000);
    }

    scaler.resetAll();

    EXPECT_FLOAT_EQ(scaler.getScaledDifficulty(GameType::SIGNAL_ECHO), 0.0f);
    EXPECT_FLOAT_EQ(scaler.getScaledDifficulty(GameType::GHOST_RUNNER), 0.0f);
    EXPECT_FLOAT_EQ(scaler.getScaledDifficulty(GameType::CIPHER_PATH), 0.0f);
}

// ============================================
// EDGE CASE TESTS
// ============================================

TEST(DifficultyScalerTests, InvalidGameTypeHandled) {
    DifficultyScaler scaler;

    // Cast to invalid enum value
    GameType invalidGame = static_cast<GameType>(999);

    // Should not crash
    scaler.recordResult(invalidGame, true, 1000);
    float scale = scaler.getScaledDifficulty(invalidGame);
    EXPECT_FLOAT_EQ(scale, 0.0f);
}

TEST(DifficultyScalerTests, ZeroCompletionTime) {
    DifficultyScaler scaler;

    // Record with 0ms completion time
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 0);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 0);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 0);

    PerformanceMetrics metrics = scaler.getMetrics(GameType::SIGNAL_ECHO);
    EXPECT_FLOAT_EQ(metrics.avgCompletionTime, 0.0f);
}

TEST(DifficultyScalerTests, VeryLongCompletionTime) {
    DifficultyScaler scaler;

    // Record with max uint32_t time
    scaler.recordResult(GameType::SIGNAL_ECHO, true, UINT32_MAX);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, UINT32_MAX);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, UINT32_MAX);

    PerformanceMetrics metrics = scaler.getMetrics(GameType::SIGNAL_ECHO);
    EXPECT_GT(metrics.avgCompletionTime, 0.0f);
}

TEST(DifficultyScalerTests, HighWinRateTriggersIncrease) {
    DifficultyScaler scaler;

    // Get to 5 results with >80% win rate
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    scaler.recordResult(GameType::SIGNAL_ECHO, false, 1000);  // 80% win rate

    float initialScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    // One more win pushes over 80%
    scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);

    float newScale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    EXPECT_GT(newScale, initialScale);
}

TEST(DifficultyScalerTests, LowWinRateTriggersDecrease) {
    DifficultyScaler scaler;

    // Start at higher difficulty
    for (int i = 0; i < 10; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    }

    // Now get <40% win rate over 5+ games
    for (int i = 0; i < 10; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, false, 1000);
    }

    float scale = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    // Should have decreased significantly
    PerformanceMetrics metrics = scaler.getMetrics(GameType::SIGNAL_ECHO);
    EXPECT_LT(metrics.recentWinRate, 0.40f);
    EXPECT_LT(scale, 0.5f);  // Should be lower than starting point
}

TEST(DifficultyScalerTests, GetCurrentScaleMatchesGetScaledDifficulty) {
    DifficultyScaler scaler;

    for (int i = 0; i < 5; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    }

    float scale1 = scaler.getCurrentScale(GameType::SIGNAL_ECHO);
    float scale2 = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    EXPECT_FLOAT_EQ(scale1, scale2);
}

TEST(DifficultyScalerTests, GetMetricsReturnsCorrectData) {
    DifficultyScaler scaler;

    scaler.recordResult(GameType::GHOST_RUNNER, true, 1500);
    scaler.recordResult(GameType::GHOST_RUNNER, false, 2000);
    scaler.recordResult(GameType::GHOST_RUNNER, true, 1800);

    PerformanceMetrics metrics = scaler.getMetrics(GameType::GHOST_RUNNER);

    EXPECT_EQ(metrics.totalPlayed, 3);
    EXPECT_EQ(metrics.resultCount, 3);
    EXPECT_FLOAT_EQ(metrics.recentWinRate, 2.0f / 3.0f);
    EXPECT_FLOAT_EQ(metrics.avgCompletionTime, (1500.0f + 2000.0f + 1800.0f) / 3.0f);
}
