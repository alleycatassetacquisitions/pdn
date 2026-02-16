#pragma once
#include <gtest/gtest.h>
#include "utils/UUID.h"
#include <string>
#include <set>
#include <cstring>

/*
 * UUID Tests
 *
 * Tests for the UUID library covering:
 * - UUID generation and format validation
 * - Uniqueness of generated UUIDs
 * - Seeding behavior and determinism
 * - Mode switching (RANDOM vs VERSION4)
 * - Edge cases and boundary conditions
 */

// ============================================
// UUID FORMAT VALIDATION TESTS
// ============================================

TEST(UUIDTests, GeneratesValidVersion4Format) {
    UUID uuid(12345);
    char* uuidStr = uuid.toCharArray();

    // UUID format: 8-4-4-4-12 (36 chars total with hyphens)
    ASSERT_EQ(strlen(uuidStr), 36);

    // Check hyphen positions (at indices 8, 13, 18, 23)
    EXPECT_EQ(uuidStr[8], '-');
    EXPECT_EQ(uuidStr[13], '-');
    EXPECT_EQ(uuidStr[18], '-');
    EXPECT_EQ(uuidStr[23], '-');

    // Check all other characters are hex digits (0-9, a-f)
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        char c = uuidStr[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Character at position " << i << " is '" << c << "', expected hex digit";
    }
}

TEST(UUIDTests, Version4HasCorrectVersionBits) {
    UUID uuid(12345);
    uuid.setVersion4Mode();
    uuid.generate();
    char* uuidStr = uuid.toCharArray();

    // Version 4: character at position 14 should be '4'
    // Format: xxxxxxxx-xxxx-4xxx-xxxx-xxxxxxxxxxxx
    EXPECT_EQ(uuidStr[14], '4') << "UUID: " << uuidStr;
}

TEST(UUIDTests, Version4HasCorrectVariantBits) {
    UUID uuid(12345);
    uuid.setVersion4Mode();
    uuid.generate();
    char* uuidStr = uuid.toCharArray();

    // Variant bits: character at position 19 should be 8, 9, a, or b
    // Format: xxxxxxxx-xxxx-4xxx-Yxxx-xxxxxxxxxxxx (Y = 8, 9, a, b)
    char variantChar = uuidStr[19];
    EXPECT_TRUE(variantChar == '8' || variantChar == '9' ||
                variantChar == 'a' || variantChar == 'b')
        << "Variant character is '" << variantChar << "', expected 8/9/a/b. UUID: " << uuidStr;
}

// ============================================
// UNIQUENESS TESTS
// ============================================

TEST(UUIDTests, GeneratesUniqueUUIDs) {
    std::set<std::string> uuidSet;
    const int numUUIDs = 100;

    // Generate 100 UUIDs with different seeds
    for (int i = 0; i < numUUIDs; i++) {
        UUID uuid(1000 + i);
        std::string uuidStr(uuid.toCharArray());
        uuidSet.insert(uuidStr);
    }

    // All should be unique
    EXPECT_EQ(uuidSet.size(), numUUIDs);
}

TEST(UUIDTests, MultipleGenerationsProduceDifferentUUIDs) {
    UUID uuid(42);

    std::set<std::string> uuidSet;
    for (int i = 0; i < 50; i++) {
        uuid.generate();
        std::string uuidStr(uuid.toCharArray());
        uuidSet.insert(uuidStr);
    }

    // All generations should be unique
    EXPECT_EQ(uuidSet.size(), 50);
}

// ============================================
// SEEDING TESTS
// ============================================

TEST(UUIDTests, SameSeedProducesSameSequence) {
    UUID uuid1(12345);
    UUID uuid2(12345);

    // Re-seed both with same values
    uuid1.seed(100, 200);
    uuid2.seed(100, 200);

    // Generate and compare
    uuid1.generate();
    uuid2.generate();

    std::string str1(uuid1.toCharArray());
    std::string str2(uuid2.toCharArray());

    EXPECT_EQ(str1, str2) << "Same seed should produce same UUID";
}

TEST(UUIDTests, DifferentSeedsProduceDifferentUUIDs) {
    UUID uuid1(12345);
    UUID uuid2(12345);

    // Seed with different values
    uuid1.seed(100, 200);
    uuid2.seed(300, 400);

    // Generate and compare
    uuid1.generate();
    uuid2.generate();

    std::string str1(uuid1.toCharArray());
    std::string str2(uuid2.toCharArray());

    EXPECT_NE(str1, str2) << "Different seeds should produce different UUIDs";
}

TEST(UUIDTests, ZeroSeedIsHandledCorrectly) {
    UUID uuid(12345);

    // Seed with zeros - should be converted to non-zero values
    uuid.seed(0, 0);
    uuid.generate();

    char* uuidStr = uuid.toCharArray();
    ASSERT_EQ(strlen(uuidStr), 36);
    EXPECT_NE(std::string(uuidStr), "");
}

// ============================================
// MODE TESTS
// ============================================

TEST(UUIDTests, DefaultModeIsVersion4) {
    UUID uuid(12345);
    EXPECT_EQ(uuid.getMode(), UUID_MODE_VERSION4);
}

TEST(UUIDTests, CanSwitchToRandomMode) {
    UUID uuid(12345);
    uuid.setRandomMode();
    EXPECT_EQ(uuid.getMode(), UUID_MODE_RANDOM);
}

TEST(UUIDTests, CanSwitchToVersion4Mode) {
    UUID uuid(12345);
    uuid.setRandomMode();
    uuid.setVersion4Mode();
    EXPECT_EQ(uuid.getMode(), UUID_MODE_VERSION4);
}

TEST(UUIDTests, RandomModeSkipsVersionBits) {
    UUID uuid(12345);
    uuid.setRandomMode();
    uuid.generate();

    char* uuidStr = uuid.toCharArray();

    // In RANDOM mode, position 14 might not be '4'
    // This is expected behavior - just verify it generates something valid
    ASSERT_EQ(strlen(uuidStr), 36);
    EXPECT_EQ(uuidStr[8], '-');
    EXPECT_EQ(uuidStr[13], '-');
    EXPECT_EQ(uuidStr[18], '-');
    EXPECT_EQ(uuidStr[23], '-');
}

// ============================================
// EDGE CASES
// ============================================

TEST(UUIDTests, ConstructorGeneratesInitialUUID) {
    UUID uuid(999);
    char* uuidStr = uuid.toCharArray();

    // Constructor should automatically generate a UUID
    ASSERT_EQ(strlen(uuidStr), 36);
    EXPECT_NE(std::string(uuidStr), "");
}

TEST(UUIDTests, ToCharArrayReturnsSameBufferAcrossCalls) {
    UUID uuid(12345);
    char* ptr1 = uuid.toCharArray();
    char* ptr2 = uuid.toCharArray();

    // Should return pointer to same internal buffer
    EXPECT_EQ(ptr1, ptr2);
}

TEST(UUIDTests, GenerateOverwritesPreviousUUID) {
    UUID uuid(42);

    uuid.generate();
    std::string first(uuid.toCharArray());

    uuid.generate();
    std::string second(uuid.toCharArray());

    // Should be different (extremely high probability)
    EXPECT_NE(first, second);
}

TEST(UUIDTests, RapidSequentialGeneration) {
    UUID uuid(7777);
    std::set<std::string> uuidSet;

    // Generate many UUIDs rapidly
    for (int i = 0; i < 1000; i++) {
        uuid.generate();
        uuidSet.insert(std::string(uuid.toCharArray()));
    }

    // Should still be unique
    EXPECT_EQ(uuidSet.size(), 1000);
}
