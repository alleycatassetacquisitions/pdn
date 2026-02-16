#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/sequence-provider.hpp"
#include "device/device-types.hpp"
#include <memory>

/*
 * SequenceProvider Tests
 *
 * Tests for sequence generation system covering:
 * - LocalSequenceProvider: seeded PRNG generation
 * - SequenceCache: storage and retrieval
 * - Determinism and reproducibility
 * - Edge cases and boundary conditions
 */

// ============================================
// SEQUENCE DATA TESTS
// ============================================

TEST(SequenceDataTests, DefaultConstructor) {
    SequenceData data;

    EXPECT_EQ(data.gameType, GameType::SIGNAL_ECHO);
    EXPECT_EQ(data.difficulty, Difficulty::EASY);
    EXPECT_EQ(data.seed, 0);
    EXPECT_EQ(data.version, 1);
    EXPECT_TRUE(data.boolSequence.empty());
    EXPECT_TRUE(data.grid.empty());
}

// ============================================
// LOCAL SEQUENCE PROVIDER TESTS
// ============================================

TEST(LocalSequenceProviderTests, GeneratesSignalEchoSequenceEasy) {
    LocalSequenceProvider provider(12345);
    SequenceData data;

    bool success = provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data);

    EXPECT_TRUE(success);
    EXPECT_EQ(data.gameType, GameType::SIGNAL_ECHO);
    EXPECT_EQ(data.difficulty, Difficulty::EASY);
    EXPECT_EQ(data.boolSequence.size(), 4);  // Easy mode = 4 elements
    EXPECT_EQ(data.seed, 12345);
    EXPECT_EQ(data.version, 1);
}

TEST(LocalSequenceProviderTests, GeneratesSignalEchoSequenceHard) {
    LocalSequenceProvider provider(12345);
    SequenceData data;

    bool success = provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::HARD, data);

    EXPECT_TRUE(success);
    EXPECT_EQ(data.gameType, GameType::SIGNAL_ECHO);
    EXPECT_EQ(data.difficulty, Difficulty::HARD);
    EXPECT_EQ(data.boolSequence.size(), 8);  // Hard mode = 8 elements
}

TEST(LocalSequenceProviderTests, GeneratesGhostRunnerMazeEasy) {
    LocalSequenceProvider provider(12345);
    SequenceData data;

    bool success = provider.fetchSequence(GameType::GHOST_RUNNER, Difficulty::EASY, data);

    EXPECT_TRUE(success);
    EXPECT_EQ(data.gameType, GameType::GHOST_RUNNER);
    EXPECT_EQ(data.difficulty, Difficulty::EASY);
    EXPECT_EQ(data.grid.size(), 5);  // 5x5 maze
    EXPECT_EQ(data.grid[0].size(), 5);
}

TEST(LocalSequenceProviderTests, GeneratesGhostRunnerMazeHard) {
    LocalSequenceProvider provider(12345);
    SequenceData data;

    bool success = provider.fetchSequence(GameType::GHOST_RUNNER, Difficulty::HARD, data);

    EXPECT_TRUE(success);
    EXPECT_EQ(data.grid.size(), 7);  // 7x7 maze
    EXPECT_EQ(data.grid[0].size(), 7);
}

TEST(LocalSequenceProviderTests, GhostRunnerMazeStartAndEndAreOpen) {
    LocalSequenceProvider provider(12345);
    SequenceData data;

    provider.fetchSequence(GameType::GHOST_RUNNER, Difficulty::EASY, data);

    // Start (0,0) and end (size-1, size-1) should be open (0)
    EXPECT_EQ(data.grid[0][0], 0);
    EXPECT_EQ(data.grid[4][4], 0);
}

TEST(LocalSequenceProviderTests, UnsupportedGameTypeReturnsFalse) {
    LocalSequenceProvider provider(12345);
    SequenceData data;

    // Try unsupported game type
    bool success = provider.fetchSequence(GameType::SPIKE_VECTOR, Difficulty::EASY, data);

    EXPECT_FALSE(success);
}

// ============================================
// DETERMINISM TESTS
// ============================================

TEST(LocalSequenceProviderTests, SameSeedProducesSameInitialState) {
    // Note: LocalSequenceProvider uses global rand() state, so true determinism
    // requires creating provider, generating sequence, then creating new provider.
    LocalSequenceProvider provider1(99999);
    SequenceData data1;
    provider1.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data1);

    // Create new provider with same seed (resets rand() state)
    LocalSequenceProvider provider2(99999);
    SequenceData data2;
    provider2.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data2);

    ASSERT_EQ(data1.boolSequence.size(), data2.boolSequence.size());

    for (size_t i = 0; i < data1.boolSequence.size(); i++) {
        EXPECT_EQ(data1.boolSequence[i], data2.boolSequence[i])
            << "Mismatch at index " << i;
    }
}

TEST(LocalSequenceProviderTests, SameSeedProducesSameMaze) {
    // Create provider, generate, then create new provider with same seed
    LocalSequenceProvider provider1(77777);
    SequenceData data1;
    provider1.fetchSequence(GameType::GHOST_RUNNER, Difficulty::HARD, data1);

    LocalSequenceProvider provider2(77777);
    SequenceData data2;
    provider2.fetchSequence(GameType::GHOST_RUNNER, Difficulty::HARD, data2);

    ASSERT_EQ(data1.grid.size(), data2.grid.size());

    for (size_t i = 0; i < data1.grid.size(); i++) {
        ASSERT_EQ(data1.grid[i].size(), data2.grid[i].size());
        for (size_t j = 0; j < data1.grid[i].size(); j++) {
            EXPECT_EQ(data1.grid[i][j], data2.grid[i][j])
                << "Maze mismatch at (" << i << "," << j << ")";
        }
    }
}

TEST(LocalSequenceProviderTests, DifferentSeedsProduceDifferentSequences) {
    LocalSequenceProvider provider1(11111);
    LocalSequenceProvider provider2(22222);

    SequenceData data1, data2;
    provider1.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data1);
    provider2.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data2);

    ASSERT_EQ(data1.boolSequence.size(), data2.boolSequence.size());

    // Should be different (not guaranteed but extremely likely)
    bool foundDifference = false;
    for (size_t i = 0; i < data1.boolSequence.size(); i++) {
        if (data1.boolSequence[i] != data2.boolSequence[i]) {
            foundDifference = true;
            break;
        }
    }

    EXPECT_TRUE(foundDifference) << "Different seeds should produce different sequences";
}

TEST(LocalSequenceProviderTests, ZeroSeedIsHandled) {
    LocalSequenceProvider provider(0);
    SequenceData data;

    bool success = provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data);

    EXPECT_TRUE(success);
    EXPECT_EQ(data.boolSequence.size(), 4);
    EXPECT_EQ(data.seed, 0);
}

TEST(LocalSequenceProviderTests, MultipleGenerationsProduceDifferentSequences) {
    LocalSequenceProvider provider(55555);

    SequenceData data1, data2, data3;
    provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data1);
    provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data2);
    provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data3);

    // Since rand() state advances, sequences should differ
    // Check that at least one is different from the first
    bool data2Different = false;
    bool data3Different = false;

    for (size_t i = 0; i < data1.boolSequence.size(); i++) {
        if (data1.boolSequence[i] != data2.boolSequence[i]) {
            data2Different = true;
        }
        if (data1.boolSequence[i] != data3.boolSequence[i]) {
            data3Different = true;
        }
    }

    // At least one should be different (very high probability)
    EXPECT_TRUE(data2Different || data3Different);
}

// ============================================
// SEQUENCE CACHE TESTS
// ============================================

TEST(SequenceCacheTests, EmptyCacheHasNoEntries) {
    SequenceCache cache;

    EXPECT_FALSE(cache.hasEntry(GameType::SIGNAL_ECHO, Difficulty::EASY));
    EXPECT_FALSE(cache.hasEntry(GameType::GHOST_RUNNER, Difficulty::HARD));
}

TEST(SequenceCacheTests, StoreAndRetrieve) {
    SequenceCache cache;
    SequenceData original;
    original.gameType = GameType::SIGNAL_ECHO;
    original.difficulty = Difficulty::EASY;
    original.boolSequence = {true, false, true, false};
    original.seed = 12345;
    original.version = 1;

    cache.store(GameType::SIGNAL_ECHO, Difficulty::EASY, original);

    EXPECT_TRUE(cache.hasEntry(GameType::SIGNAL_ECHO, Difficulty::EASY));

    SequenceData retrieved;
    bool success = cache.retrieve(GameType::SIGNAL_ECHO, Difficulty::EASY, retrieved);

    EXPECT_TRUE(success);
    EXPECT_EQ(retrieved.gameType, original.gameType);
    EXPECT_EQ(retrieved.difficulty, original.difficulty);
    EXPECT_EQ(retrieved.seed, original.seed);
    EXPECT_EQ(retrieved.version, original.version);
    EXPECT_EQ(retrieved.boolSequence, original.boolSequence);
}

TEST(SequenceCacheTests, RetrieveNonExistentReturnsFalse) {
    SequenceCache cache;
    SequenceData data;

    bool success = cache.retrieve(GameType::CIPHER_PATH, Difficulty::HARD, data);

    EXPECT_FALSE(success);
}

TEST(SequenceCacheTests, DifferentKeysStoredSeparately) {
    SequenceCache cache;

    SequenceData data1;
    data1.gameType = GameType::SIGNAL_ECHO;
    data1.difficulty = Difficulty::EASY;
    data1.seed = 111;

    SequenceData data2;
    data2.gameType = GameType::SIGNAL_ECHO;
    data2.difficulty = Difficulty::HARD;
    data2.seed = 222;

    SequenceData data3;
    data3.gameType = GameType::GHOST_RUNNER;
    data3.difficulty = Difficulty::EASY;
    data3.seed = 333;

    cache.store(GameType::SIGNAL_ECHO, Difficulty::EASY, data1);
    cache.store(GameType::SIGNAL_ECHO, Difficulty::HARD, data2);
    cache.store(GameType::GHOST_RUNNER, Difficulty::EASY, data3);

    SequenceData retrieved;

    cache.retrieve(GameType::SIGNAL_ECHO, Difficulty::EASY, retrieved);
    EXPECT_EQ(retrieved.seed, 111);

    cache.retrieve(GameType::SIGNAL_ECHO, Difficulty::HARD, retrieved);
    EXPECT_EQ(retrieved.seed, 222);

    cache.retrieve(GameType::GHOST_RUNNER, Difficulty::EASY, retrieved);
    EXPECT_EQ(retrieved.seed, 333);
}

TEST(SequenceCacheTests, OverwriteExistingEntry) {
    SequenceCache cache;

    SequenceData data1;
    data1.seed = 100;
    cache.store(GameType::SIGNAL_ECHO, Difficulty::EASY, data1);

    SequenceData data2;
    data2.seed = 200;
    cache.store(GameType::SIGNAL_ECHO, Difficulty::EASY, data2);

    SequenceData retrieved;
    cache.retrieve(GameType::SIGNAL_ECHO, Difficulty::EASY, retrieved);

    EXPECT_EQ(retrieved.seed, 200);  // Should have overwritten
}

TEST(SequenceCacheTests, ClearRemovesAllEntries) {
    SequenceCache cache;

    SequenceData data;
    cache.store(GameType::SIGNAL_ECHO, Difficulty::EASY, data);
    cache.store(GameType::GHOST_RUNNER, Difficulty::HARD, data);
    cache.store(GameType::CIPHER_PATH, Difficulty::EASY, data);

    EXPECT_TRUE(cache.hasEntry(GameType::SIGNAL_ECHO, Difficulty::EASY));
    EXPECT_TRUE(cache.hasEntry(GameType::GHOST_RUNNER, Difficulty::HARD));
    EXPECT_TRUE(cache.hasEntry(GameType::CIPHER_PATH, Difficulty::EASY));

    cache.clear();

    EXPECT_FALSE(cache.hasEntry(GameType::SIGNAL_ECHO, Difficulty::EASY));
    EXPECT_FALSE(cache.hasEntry(GameType::GHOST_RUNNER, Difficulty::HARD));
    EXPECT_FALSE(cache.hasEntry(GameType::CIPHER_PATH, Difficulty::EASY));
}

TEST(SequenceCacheTests, StoreComplexMazeData) {
    SequenceCache cache;

    SequenceData data;
    data.gameType = GameType::GHOST_RUNNER;
    data.difficulty = Difficulty::HARD;
    data.grid = {
        {0, 1, 0},
        {1, 0, 1},
        {0, 0, 0}
    };
    data.seed = 99999;

    cache.store(GameType::GHOST_RUNNER, Difficulty::HARD, data);

    SequenceData retrieved;
    cache.retrieve(GameType::GHOST_RUNNER, Difficulty::HARD, retrieved);

    ASSERT_EQ(retrieved.grid.size(), 3);
    EXPECT_EQ(retrieved.grid[0], std::vector<int>({0, 1, 0}));
    EXPECT_EQ(retrieved.grid[1], std::vector<int>({1, 0, 1}));
    EXPECT_EQ(retrieved.grid[2], std::vector<int>({0, 0, 0}));
}

// ============================================
// EDGE CASE TESTS
// ============================================

TEST(LocalSequenceProviderTests, LargeSeedValue) {
    LocalSequenceProvider provider(UINT32_MAX);
    SequenceData data;

    bool success = provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data);

    EXPECT_TRUE(success);
    EXPECT_EQ(data.seed, UINT32_MAX);
}

TEST(LocalSequenceProviderTests, ConsecutiveCallsAdvanceRNG) {
    LocalSequenceProvider provider(42);

    SequenceData data1, data2, data3;
    provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data1);
    provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data2);
    provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, data3);

    // All should be different (extremely likely)
    EXPECT_NE(data1.boolSequence, data2.boolSequence);
    EXPECT_NE(data2.boolSequence, data3.boolSequence);
    EXPECT_NE(data1.boolSequence, data3.boolSequence);
}

TEST(LocalSequenceProviderTests, MixedGameTypeCalls) {
    LocalSequenceProvider provider(12345);

    SequenceData signalData, ghostData;
    provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, signalData);
    provider.fetchSequence(GameType::GHOST_RUNNER, Difficulty::EASY, ghostData);

    // Both should succeed independently
    EXPECT_EQ(signalData.boolSequence.size(), 4);
    EXPECT_EQ(ghostData.grid.size(), 5);
}

TEST(SequenceCacheTests, HasEntryDoesNotModifyCache) {
    SequenceCache cache;

    EXPECT_FALSE(cache.hasEntry(GameType::SIGNAL_ECHO, Difficulty::EASY));

    // Multiple checks should not change state
    for (int i = 0; i < 10; i++) {
        EXPECT_FALSE(cache.hasEntry(GameType::SIGNAL_ECHO, Difficulty::EASY));
    }
}

TEST(SequenceCacheTests, RetrieveDoesNotRemoveEntry) {
    SequenceCache cache;

    SequenceData data;
    data.seed = 777;
    cache.store(GameType::SIGNAL_ECHO, Difficulty::EASY, data);

    SequenceData retrieved;
    cache.retrieve(GameType::SIGNAL_ECHO, Difficulty::EASY, retrieved);

    // Should still be there
    EXPECT_TRUE(cache.hasEntry(GameType::SIGNAL_ECHO, Difficulty::EASY));

    // Retrieve again
    cache.retrieve(GameType::SIGNAL_ECHO, Difficulty::EASY, retrieved);
    EXPECT_EQ(retrieved.seed, 777);
}

TEST(LocalSequenceProviderTests, AllSupportedGameTypes) {
    LocalSequenceProvider provider(123);

    // Signal Echo
    SequenceData seData;
    EXPECT_TRUE(provider.fetchSequence(GameType::SIGNAL_ECHO, Difficulty::EASY, seData));

    // Ghost Runner
    SequenceData grData;
    EXPECT_TRUE(provider.fetchSequence(GameType::GHOST_RUNNER, Difficulty::EASY, grData));

    // Unsupported games should return false
    SequenceData svData;
    EXPECT_FALSE(provider.fetchSequence(GameType::SPIKE_VECTOR, Difficulty::EASY, svData));

    SequenceData fdData;
    EXPECT_FALSE(provider.fetchSequence(GameType::FIREWALL_DECRYPT, Difficulty::EASY, fdData));
}
