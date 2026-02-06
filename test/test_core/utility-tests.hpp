#pragma once

#include <gtest/gtest.h>
#include "id-generator.hpp"
#include "wireless/mac-functions.hpp"
#include "utils/simple-timer.hpp"
#include "device/drivers/platform-clock.hpp"

// ============================================
// Fake Platform Clock for Timer Tests
// ============================================

class FakePlatformClock : public PlatformClock {
public:
    FakePlatformClock() : currentTime(0) {}

    unsigned long milliseconds() override {
        return currentTime;
    }

    void setTime(unsigned long time) {
        currentTime = time;
    }

    void advance(unsigned long delta) {
        currentTime += delta;
    }

private:
    unsigned long currentTime;
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
    std::string uuid = "12345678-abcd-ef01-2345-6789abcdef01";
    uint8_t bytes[16];

    IdGenerator::uuidStringToBytes(uuid, bytes);

    // Verify first few bytes
    EXPECT_EQ(bytes[0], 0x12);
    EXPECT_EQ(bytes[1], 0x34);
    EXPECT_EQ(bytes[2], 0x56);
    EXPECT_EQ(bytes[3], 0x78);
    EXPECT_EQ(bytes[4], 0xab);
    EXPECT_EQ(bytes[5], 0xcd);
    EXPECT_EQ(bytes[6], 0xef);
    EXPECT_EQ(bytes[7], 0x01);
}

inline void uuidBytesToStringProducesValidFormat() {
    uint8_t bytes[16] = {
        0x12, 0x34, 0x56, 0x78,
        0xab, 0xcd,
        0xef, 0x01,
        0x23, 0x45,
        0x67, 0x89, 0xab, 0xcd, 0xef, 0x01
    };

    std::string uuid = IdGenerator::uuidBytesToString(bytes);

    // Should be in format: 8-4-4-4-12
    EXPECT_EQ(uuid.length(), 36);
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');

    // Verify content
    EXPECT_EQ(uuid, "12345678-abcd-ef01-2345-6789abcdef01");
}

inline void uuidRoundTripPreservesData() {
    // Original UUID string
    std::string original = "deadbeef-cafe-babe-1234-567890abcdef";
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
// MAC Address Tests
// ============================================

class MACTestSuite : public testing::Test {};

inline void macToStringProducesCorrectFormat() {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    const char* macStr = MacToString(mac);

    EXPECT_STREQ(macStr, "AA:BB:CC:DD:EE:FF");
}

inline void macToStringHandlesZeros() {
    uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    const char* macStr = MacToString(mac);

    EXPECT_STREQ(macStr, "00:00:00:00:00:00");
}

inline void stringToMacParsesValidFormat() {
    uint8_t mac[6];

    bool result = StringToMac("AA:BB:CC:DD:EE:FF", mac);

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
