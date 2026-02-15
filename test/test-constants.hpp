#pragma once

/*
 * Standardized test constants for PDN test suites
 *
 * This file provides valid, consistent test data for UUIDs, MAC addresses,
 * and device IDs across all test files. Using these constants ensures:
 * - All test UUIDs follow valid UUID v4 format (8-4-4-4-12 hex chars)
 * - All test MAC addresses follow valid format (XX:XX:XX:XX:XX:XX)
 * - Test data is consistent across different test suites
 * - Edge cases (empty, invalid, zero, max) are available for validation tests
 */

namespace TestConstants {
    // ============================================
    // Player UUIDs (valid UUID v4 format)
    // ============================================
    // Format: 8-4-4-4-12 hex characters with hyphens
    // These represent typical player/device identifiers

    constexpr const char* TEST_UUID_PLAYER_1 = "12345678-1234-5678-9abc-123456789abc";
    constexpr const char* TEST_UUID_PLAYER_2 = "87654321-4321-8765-cba9-cba987654321";
    constexpr const char* TEST_UUID_PLAYER_3 = "aabbccdd-aabb-ccdd-eeff-aabbccddeeff";
    constexpr const char* TEST_UUID_PLAYER_4 = "deadbeef-cafe-babe-1234-567890abcdef";

    // Hunter-specific UUIDs (for role-based tests)
    constexpr const char* TEST_UUID_HUNTER_1 = "a0b1c2d3-1234-5678-9abc-def012345678";
    constexpr const char* TEST_UUID_HUNTER_2 = "11111111-2222-3333-4444-555555555555";

    // Bounty-specific UUIDs (for role-based tests)
    constexpr const char* TEST_UUID_BOUNTY_1 = "b0a1b2c3-1234-5678-9abc-def012345678";
    constexpr const char* TEST_UUID_BOUNTY_2 = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";

    // Match UUIDs
    constexpr const char* TEST_UUID_MATCH_1 = "abcdef12-3456-7890-abcd-ef1234567890";
    constexpr const char* TEST_UUID_MATCH_2 = "fedcba98-7654-3210-fedc-ba9876543210";

    // NPC/Special entity UUIDs
    constexpr const char* TEST_UUID_NPC_1 = "00000000-0000-0000-0000-000000000001";
    constexpr const char* TEST_UUID_NPC_2 = "00000000-0000-0000-0000-000000000002";

    // Edge case UUIDs (for validation tests)
    constexpr const char* TEST_UUID_ZERO = "00000000-0000-0000-0000-000000000000";
    constexpr const char* TEST_UUID_MAX = "ffffffff-ffff-ffff-ffff-ffffffffffff";
    constexpr const char* TEST_UUID_INVALID = "not-a-valid-uuid";
    constexpr const char* TEST_UUID_EMPTY = "";
    constexpr const char* TEST_UUID_TOO_LONG = "12345678-abcd-ef01-2345-6789abcdef01-extra";
    constexpr const char* TEST_UUID_TOO_SHORT = "12345678-abcd-ef01-2345";
    constexpr const char* TEST_UUID_INVALID_HEX = "12345678-ZZZZ-ef01-2345-6789abcdef01";
    constexpr const char* TEST_UUID_MALICIOUS = "12345678-abcd-ef01-2345-6789abcdef01aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    // ============================================
    // MAC Addresses (valid format: XX:XX:XX:XX:XX:XX)
    // ============================================

    // Standard test MAC addresses
    constexpr const char* TEST_MAC_PLAYER_1 = "AA:BB:CC:DD:EE:01";
    constexpr const char* TEST_MAC_PLAYER_2 = "AA:BB:CC:DD:EE:02";
    constexpr const char* TEST_MAC_PLAYER_3 = "AA:BB:CC:DD:EE:03";

    // Hunter-specific MACs
    constexpr const char* TEST_MAC_HUNTER_1 = "AA:AA:AA:AA:AA:AA";
    constexpr const char* TEST_MAC_HUNTER_2 = "11:11:11:11:11:11";

    // Bounty-specific MACs
    constexpr const char* TEST_MAC_BOUNTY_1 = "BB:BB:BB:BB:BB:BB";
    constexpr const char* TEST_MAC_BOUNTY_2 = "22:22:22:22:22:22";

    // Default/common MAC (for backward compatibility with existing tests)
    constexpr const char* TEST_MAC_DEFAULT = "AA:BB:CC:DD:EE:FF";

    // NPC/Special MACs
    constexpr const char* TEST_MAC_NPC_1 = "11:22:33:44:55:01";
    constexpr const char* TEST_MAC_NPC_2 = "11:22:33:44:55:02";

    // Edge case MACs
    constexpr const char* TEST_MAC_ZERO = "00:00:00:00:00:00";
    constexpr const char* TEST_MAC_MAX = "FF:FF:FF:FF:FF:FF";
    constexpr const char* TEST_MAC_INVALID = "ZZ:ZZ:ZZ:ZZ:ZZ:ZZ";
    constexpr const char* TEST_MAC_EMPTY = "";

    // Binary MAC addresses (for low-level tests)
    constexpr uint8_t TEST_MAC_HUNTER_1_BYTES[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    constexpr uint8_t TEST_MAC_BOUNTY_1_BYTES[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    constexpr uint8_t TEST_MAC_DEFAULT_BYTES[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // ============================================
    // Device IDs (integer identifiers)
    // ============================================

    constexpr int TEST_DEVICE_ID_PLAYER_1 = 1;
    constexpr int TEST_DEVICE_ID_PLAYER_2 = 2;
    constexpr int TEST_DEVICE_ID_PLAYER_3 = 3;
    constexpr int TEST_DEVICE_ID_NPC_1 = 101;
    constexpr int TEST_DEVICE_ID_NPC_2 = 102;

    // ============================================
    // Simple String IDs (for non-UUID contexts)
    // ============================================

    // For tests that use simple string identifiers instead of full UUIDs
    constexpr const char* TEST_ID_PLAYER = "test-player-uuid";
    constexpr const char* TEST_ID_HUNTER = "hunter-uuid";
    constexpr const char* TEST_ID_BOUNTY = "bounty-uuid";
    constexpr const char* TEST_ID_MATCH = "match-123";

    // Simple player IDs
    constexpr const char* TEST_ID_PLAYER_1 = "1234";
    constexpr const char* TEST_ID_PLAYER_2 = "5678";
    constexpr const char* TEST_ID_PLAYER_3 = "9999";

    // Match IDs for integration tests
    constexpr const char* TEST_MATCH_ID_1 = "match-1";
    constexpr const char* TEST_MATCH_ID_2 = "match-2";
    constexpr const char* TEST_MATCH_ID_TIE = "tie-match-1234-5678-9abc-def012345678";
    constexpr const char* TEST_MATCH_ID_TIMEOUT = "timeout-match-1234-5678-9abc-def01234";
    constexpr const char* TEST_MATCH_ID_MAC = "mac-test-match-id-1234567890";
    constexpr const char* TEST_MATCH_ID_TEST = "test-match-uuid-1234567890";

    // ============================================
    // FDN Device IDs (for CLI tests)
    // ============================================

    constexpr const char* TEST_FDN_DEVICE_ID_0 = "7010";
    constexpr const char* TEST_FDN_DEVICE_ID_1 = "7011";

    // ============================================
    // Random Seeds (for reproducible tests)
    // ============================================

    constexpr unsigned long TEST_SEED_DEFAULT = 42;
    constexpr unsigned long TEST_SEED_INTEGRATION = 54321;
}
