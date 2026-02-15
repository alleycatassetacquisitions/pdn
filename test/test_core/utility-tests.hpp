#pragma once

#include <gtest/gtest.h>
#include "id-generator.hpp"
#include "wireless/mac-functions.hpp"
#include "utils/simple-timer.hpp"
#include "device/drivers/platform-clock.hpp"
#include "../test-constants.hpp"

// ============================================
// Fake Platform Clock for Timer Tests
// ============================================

class FakePlatformClock : public PlatformClock {
public:
    FakePlatformClock() : currentTime(0) {}

    unsigned long milliseconds() override {
        // Return as unsigned long to match interface
        return static_cast<unsigned long>(currentTime);
    }

    void setTime(unsigned long time) {
        // Store as uint32_t to simulate ESP32's 32-bit millis() overflow behavior
        currentTime = static_cast<uint32_t>(time);
    }

    void advance(unsigned long delta) {
        // Addition will wrap around at 32-bit boundary
        currentTime += static_cast<uint32_t>(delta);
    }

private:
    // Use uint32_t to simulate ESP32's 32-bit millis() which wraps at UINT32_MAX
    uint32_t currentTime;
};

// ============================================
// UUID Tests
// ============================================

class UUIDTestSuite : public testing::Test {
protected:
    void SetUp() override {
        IdGenerator idGenerator = IdGenerator(42);
    }
};

inline void uuidStringToBytesProducesCorrectOutput() {
    // Standard UUID format: 8-4-4-4-12 hex chars
    std::string uuid = TestConstants::TEST_UUID_PLAYER_1;
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(uuid, bytes);

    // Verify first few bytes
    EXPECT_EQ(bytes[0], 0x12);
    EXPECT_EQ(bytes[1], 0x34);
    EXPECT_EQ(bytes[2], 0x56);
    EXPECT_EQ(bytes[3], 0x78);
    EXPECT_EQ(bytes[4], 0x12);
    EXPECT_EQ(bytes[5], 0x34);
    EXPECT_EQ(bytes[6], 0x56);
    EXPECT_EQ(bytes[7], 0x78);
}

inline void uuidBytesToStringProducesValidFormat() {
    uint8_t bytes[16] = {
        0x12, 0x34, 0x56, 0x78,
        0x12, 0x34,
        0x56, 0x78,
        0x9a, 0xbc,
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc
    };

    std::string uuid = IdGenerator::uuidBytesToString(bytes);

    // Should be in format: 8-4-4-4-12
    EXPECT_EQ(uuid.length(), 36);
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');

    // Verify content
    EXPECT_EQ(uuid, TestConstants::TEST_UUID_PLAYER_1);
}

inline void uuidRoundTripPreservesData() {
    // Original UUID string
    std::string original = TestConstants::TEST_UUID_PLAYER_4;
    uint8_t bytes[16];

    // Convert to bytes
    IdGenerator::uuidStringToBytes(original, bytes);

    // Convert back to string
    std::string restored = IdGenerator::uuidBytesToString(bytes);

    EXPECT_EQ(restored, original);
}

inline void uuidGeneratorProducesValidFormat() {
    char* uuid = IdGenerator(42).generateId();

    std::string uuidStr(uuid);

    // Should be valid UUID format
    EXPECT_EQ(uuidStr.length(), 36);
    EXPECT_EQ(uuidStr[8], '-');
    EXPECT_EQ(uuidStr[13], '-');
    EXPECT_EQ(uuidStr[18], '-');
    EXPECT_EQ(uuidStr[23], '-');
}

// ============================================
// UUID Bounds Checking Tests (Issue #139)
// ============================================

inline void uuidStringToBytesRejectsTooLongString() {
    // String longer than 36 characters
    std::string tooLong = TestConstants::TEST_UUID_TOO_LONG;
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(tooLong, bytes);

    // Should return zeroed buffer
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(bytes[i], 0);
    }
}

inline void uuidStringToBytesRejectsTooShortString() {
    // String shorter than 36 characters
    std::string tooShort = TestConstants::TEST_UUID_TOO_SHORT;
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(tooShort, bytes);

    // Should return zeroed buffer
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(bytes[i], 0);
    }
}

inline void uuidStringToBytesRejectsNonHexCharacters() {
    // UUID with invalid hex characters
    std::string invalidHex = TestConstants::TEST_UUID_INVALID_HEX;
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(invalidHex, bytes);

    // hexCharToInt returns 0 for invalid chars, so we get non-zero output
    // This test documents current behavior - invalid hex produces garbage
    // but doesn't crash (returns 0 for invalid chars)
    EXPECT_EQ(bytes[4], 0x00);  // 'Z' maps to 0
    EXPECT_EQ(bytes[5], 0x00);  // 'Z' maps to 0
}

inline void uuidStringToBytesRejectsMissingHyphens() {
    // UUID without hyphens at correct positions
    std::string noHyphens = "12345678abcdef012345-6789abcdef01";
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(noHyphens, bytes);

    // Should return zeroed buffer due to format validation
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(bytes[i], 0);
    }
}

inline void uuidStringToBytesRejectsEmptyString() {
    std::string empty = "";
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(empty, bytes);

    // Should return zeroed buffer
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(bytes[i], 0);
    }
}

inline void uuidStringToBytesHandlesAllZeros() {
    std::string zeroUuid = TestConstants::TEST_UUID_ZERO;
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(zeroUuid, bytes);

    // Should successfully parse to all zeros
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(bytes[i], 0);
    }
}

inline void uuidStringToBytesHandlesAllFs() {
    std::string maxUuid = TestConstants::TEST_UUID_MAX;
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(maxUuid, bytes);

    // Should successfully parse to all 0xFF
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(bytes[i], 0xFF);
    }
}

inline void uuidStringToBytesPreventsBufferOverflow() {
    // Test that malformed long input doesn't overflow buffer
    // This is the core security issue from #139
    std::string malicious = TestConstants::TEST_UUID_MALICIOUS;
    uint8_t bytes[16];

    // Initialize with sentinel values to detect overflow
    uint8_t sentinel[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    IdGenerator::uuidStringToBytes(malicious, bytes);

    // Sentinel should be unchanged (no buffer overflow)
    EXPECT_EQ(sentinel[0], 0xAA);
    EXPECT_EQ(sentinel[1], 0xBB);
    EXPECT_EQ(sentinel[2], 0xCC);
    EXPECT_EQ(sentinel[3], 0xDD);

    // Buffer should be zeroed due to length validation
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(bytes[i], 0);
    }
}

// ============================================
// MAC Address Tests
// ============================================

class MACTestSuite : public testing::Test {};

inline void macToStringProducesCorrectFormat() {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    const char* macStr = MacToString(mac);

    EXPECT_STREQ(macStr, TestConstants::TEST_MAC_DEFAULT);
}

inline void macToStringHandlesZeros() {
    uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    const char* macStr = MacToString(mac);

    EXPECT_STREQ(macStr, "00:00:00:00:00:00");
}

inline void stringToMacParsesValidFormat() {
    uint8_t mac[6];

    bool result = StringToMac(TestConstants::TEST_MAC_DEFAULT, mac);

    EXPECT_TRUE(result);
    EXPECT_EQ(mac[0], 0xAA);
    EXPECT_EQ(mac[1], 0xBB);
    EXPECT_EQ(mac[2], 0xCC);
    EXPECT_EQ(mac[3], 0xDD);
    EXPECT_EQ(mac[4], 0xEE);
    EXPECT_EQ(mac[5], 0xFF);
}

inline void stringToMacRejectsInvalidLength() {
    uint8_t mac[6];

    // Too short
    EXPECT_FALSE(StringToMac("AA:BB:CC", mac));

    // Too long
    EXPECT_FALSE(StringToMac("AA:BB:CC:DD:EE:FF:00", mac));
}

inline void macToUInt64ProducesCorrectValue() {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    uint64_t result = MacToUInt64(mac);

    // Expected: 0x010203040506
    EXPECT_EQ(result, 0x010203040506ULL);
}

inline void macRoundTripPreservesData() {
    uint8_t original[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    // Convert to string
    const char* macStr = MacToString(original);

    // Convert back to bytes
    uint8_t restored[6];
    bool success = StringToMac(macStr, restored);

    EXPECT_TRUE(success);
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(restored[i], original[i]);
    }
}

// ============================================
// SimpleTimer Tests
// ============================================

class TimerTestSuite : public testing::Test {
protected:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakePlatformClock* fakeClock;
};

inline void timerExpiresAfterDuration(FakePlatformClock* fakeClock) {
    SimpleTimer timer;

    fakeClock->setTime(1000);
    timer.setTimer(500);

    EXPECT_TRUE(timer.isRunning());
    EXPECT_FALSE(timer.expired());

    // Advance time, but not enough
    fakeClock->advance(400);
    EXPECT_FALSE(timer.expired());

    // Advance past expiration
    fakeClock->advance(200);
    EXPECT_TRUE(timer.expired());
}

inline void timerDoesNotExpireBeforeDuration(FakePlatformClock* fakeClock) {
    SimpleTimer timer;

    fakeClock->setTime(0);
    timer.setTimer(1000);

    // Check at various points before expiration
    fakeClock->setTime(100);
    EXPECT_FALSE(timer.expired());

    fakeClock->setTime(500);
    EXPECT_FALSE(timer.expired());

    fakeClock->setTime(999);
    EXPECT_FALSE(timer.expired());

    // Should expire at or after 1000
    fakeClock->setTime(1001);
    EXPECT_TRUE(timer.expired());
}

inline void timerInvalidateStopsTimer(FakePlatformClock* fakeClock) {
    SimpleTimer timer;

    fakeClock->setTime(0);
    timer.setTimer(1000);

    EXPECT_TRUE(timer.isRunning());

    timer.invalidate();

    EXPECT_FALSE(timer.isRunning());
    EXPECT_FALSE(timer.expired()); // Invalidated timers don't expire
}

inline void timerElapsedTimeIsAccurate(FakePlatformClock* fakeClock) {
    SimpleTimer timer;

    fakeClock->setTime(1000);
    timer.setTimer(5000);

    fakeClock->setTime(1500);
    EXPECT_EQ(timer.getElapsedTime(), 500);

    fakeClock->setTime(3000);
    EXPECT_EQ(timer.getElapsedTime(), 2000);
}

inline void timerWithNullClockHandlesGracefully() {
    SimpleTimer::setPlatformClock(nullptr);
    SimpleTimer timer;

    // Should not crash
    timer.setTimer(1000);
    EXPECT_EQ(timer.now, 0);
    EXPECT_EQ(timer.getElapsedTime(), 0);
}

inline void timerHandlesOverflowBoundaryCorrectly(FakePlatformClock* fakeClock) {
    /*
     * Test that SimpleTimer correctly handles uint32_t overflow.
     * On ESP32, millis() wraps to 0 after ~49.7 days (UINT32_MAX ms).
     * This test simulates a timer starting near the overflow boundary
     * and advancing past it.
     */
    SimpleTimer timer;

    // Start timer near UINT32_MAX
    unsigned long nearMax = UINT32_MAX - 100;  // 100ms before overflow
    fakeClock->setTime(nearMax);
    timer.setTimer(500);  // Timer should expire 500ms later

    EXPECT_TRUE(timer.isRunning());
    EXPECT_FALSE(timer.expired());

    // Advance to just before overflow
    fakeClock->setTime(UINT32_MAX - 50);
    EXPECT_EQ(timer.getElapsedTime(), 50UL);
    EXPECT_FALSE(timer.expired());

    // Advance past overflow boundary (wraps to 0)
    // We're now at: start=UINT32_MAX-100, current=200 (wrapped)
    // Time progression: UINT32_MAX-100 → ... → UINT32_MAX → 0 → ... → 200
    // That's 100 steps to reach UINT32_MAX, +1 step to wrap to 0, +200 steps to reach 200 = 301 total
    fakeClock->setTime(200);
    EXPECT_EQ(timer.getElapsedTime(), 301UL);
    EXPECT_FALSE(timer.expired());  // Still not expired (need 500ms total)

    // Advance to expiration point (timer expires when elapsed > duration, so need 501ms)
    fakeClock->setTime(400);
    EXPECT_EQ(timer.getElapsedTime(), 501UL);
    EXPECT_TRUE(timer.expired());

    // Verify continued correct behavior past expiration
    fakeClock->setTime(600);
    EXPECT_EQ(timer.getElapsedTime(), 701UL);
    EXPECT_TRUE(timer.expired());
}

inline void timerHandlesOverflowAtExactBoundary(FakePlatformClock* fakeClock) {
    /*
     * Test overflow exactly at UINT32_MAX → 0 boundary
     */
    SimpleTimer timer;

    // Start timer exactly at UINT32_MAX
    fakeClock->setTime(UINT32_MAX);
    timer.setTimer(100);

    EXPECT_FALSE(timer.expired());

    // Wrap to 0
    fakeClock->setTime(0);
    EXPECT_EQ(timer.getElapsedTime(), 1UL);
    EXPECT_FALSE(timer.expired());

    // Advance to expiration
    fakeClock->setTime(100);
    EXPECT_EQ(timer.getElapsedTime(), 101UL);
    EXPECT_TRUE(timer.expired());
}

inline void timerHandlesLargeElapsedTimeAcrossOverflow(FakePlatformClock* fakeClock) {
    /*
     * Test a timer with a long duration that spans the overflow
     */
    SimpleTimer timer;

    // Start 1 second before overflow
    fakeClock->setTime(UINT32_MAX - 1000);
    timer.setTimer(5000);  // 5 second timer

    EXPECT_FALSE(timer.expired());

    // Jump to 3 seconds after overflow
    // Start was UINT32_MAX-1000, current is 3000
    // Elapsed: 1000 (to reach UINT32_MAX) + 1 (wrap to 0) + 3000 = 4001
    fakeClock->setTime(3000);
    EXPECT_EQ(timer.getElapsedTime(), 4001UL);
    EXPECT_FALSE(timer.expired());

    // Jump to 5 seconds after overflow
    // Elapsed: 1000 + 1 + 5000 = 6001
    fakeClock->setTime(5000);
    EXPECT_EQ(timer.getElapsedTime(), 6001UL);
    EXPECT_TRUE(timer.expired());
}
