#pragma once
#include <gtest/gtest.h>
#include "utils/simple-timer.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include <memory>

/*
 * SimpleTimer Tests
 *
 * Tests for SimpleTimer utility covering:
 * - Timer lifecycle (set, expire, invalidate, reset)
 * - Elapsed time calculation
 * - Overflow handling (uint32_t wraparound)
 * - Edge cases (zero duration, max values, rapid calls)
 * - PlatformClock integration
 */

class TimerTestFixture : public ::testing::Test {
protected:
    std::unique_ptr<NativeClockDriver> clock;

    void SetUp() override {
        clock = std::make_unique<NativeClockDriver>("test-clock");
        SimpleTimer::setPlatformClock(clock.get());
    }

    void TearDown() override {
        SimpleTimer::resetClock();
    }

    // Helper to advance clock by N milliseconds
    void advanceClock(unsigned long ms) {
        unsigned long target = clock->milliseconds() + ms;
        while (clock->milliseconds() < target) {
            // Busy wait (NativeClock is based on system time)
            // In tests we can sleep to avoid busy-waiting
        }
    }
};

// ============================================
// BASIC TIMER OPERATION TESTS
// ============================================

TEST_F(TimerTestFixture, NewTimerIsNotRunning) {
    SimpleTimer timer;
    EXPECT_FALSE(timer.isRunning());
    EXPECT_FALSE(timer.expired());
}

TEST_F(TimerTestFixture, SetTimerStartsRunning) {
    SimpleTimer timer;
    timer.setTimer(1000);
    EXPECT_TRUE(timer.isRunning());
}

TEST_F(TimerTestFixture, TimerNotExpiredImmediatelyAfterSet) {
    SimpleTimer timer;
    timer.setTimer(1000);
    EXPECT_FALSE(timer.expired());
}

TEST_F(TimerTestFixture, InvalidateStopsTimer) {
    SimpleTimer timer;
    timer.setTimer(1000);
    EXPECT_TRUE(timer.isRunning());

    timer.invalidate();
    EXPECT_FALSE(timer.isRunning());
    EXPECT_FALSE(timer.expired());
}

// ============================================
// ELAPSED TIME TESTS
// ============================================

TEST_F(TimerTestFixture, GetElapsedTimeReturnsZeroWhenNoClockSet) {
    SimpleTimer::resetClock();  // Remove clock
    SimpleTimer timer;
    timer.setTimer(1000);
    EXPECT_EQ(timer.getElapsedTime(), 0);
}

TEST_F(TimerTestFixture, GetElapsedTimeIncreasesOverTime) {
    SimpleTimer timer;
    timer.setTimer(5000);

    unsigned long elapsed1 = timer.getElapsedTime();

    // Small delay
    auto start = clock->milliseconds();
    while (clock->milliseconds() - start < 10) {
        // Wait 10ms
    }

    unsigned long elapsed2 = timer.getElapsedTime();

    EXPECT_GT(elapsed2, elapsed1);
}

TEST_F(TimerTestFixture, GetElapsedTimeIsAccurate) {
    SimpleTimer timer;
    timer.setTimer(10000);

    auto start = clock->milliseconds();
    while (clock->milliseconds() - start < 50) {
        // Wait approximately 50ms
    }

    unsigned long elapsed = timer.getElapsedTime();

    // Should be close to 50ms (allow ±20ms tolerance for timing variations)
    EXPECT_GE(elapsed, 30);
    EXPECT_LE(elapsed, 100);
}

// ============================================
// EXPIRATION TESTS
// ============================================

TEST_F(TimerTestFixture, ShortTimerExpires) {
    SimpleTimer timer;
    timer.setTimer(10);  // 10ms

    // Wait longer than timer duration
    auto start = clock->milliseconds();
    while (clock->milliseconds() - start < 20) {
        // Wait 20ms
    }

    EXPECT_TRUE(timer.expired());
}

TEST_F(TimerTestFixture, ZeroDurationTimerExpiresImmediately) {
    SimpleTimer timer;
    timer.setTimer(0);

    // Even a tiny delay should cause expiration
    auto start = clock->milliseconds();
    while (clock->milliseconds() - start < 1) {
        // Wait 1ms
    }

    EXPECT_TRUE(timer.expired());
}

TEST_F(TimerTestFixture, InvalidatedTimerNeverExpires) {
    SimpleTimer timer;
    timer.setTimer(10);

    timer.invalidate();

    // Wait past duration
    auto start = clock->milliseconds();
    while (clock->milliseconds() - start < 50) {
        // Wait 50ms
    }

    EXPECT_FALSE(timer.expired());
}

// ============================================
// EDGE CASE TESTS
// ============================================

TEST_F(TimerTestFixture, MaxDurationTimer) {
    SimpleTimer timer;
    timer.setTimer(UINT32_MAX);

    // Should not expire immediately
    EXPECT_FALSE(timer.expired());
    EXPECT_TRUE(timer.isRunning());
}

TEST_F(TimerTestFixture, RapidSetAndCheck) {
    SimpleTimer timer;

    // Set and check many times in rapid succession
    for (int i = 0; i < 100; i++) {
        timer.setTimer(1000);
        EXPECT_TRUE(timer.isRunning());
        EXPECT_FALSE(timer.expired());
    }
}

TEST_F(TimerTestFixture, MultipleTimersIndependent) {
    SimpleTimer timer1, timer2;

    timer1.setTimer(100);
    timer2.setTimer(200);

    EXPECT_TRUE(timer1.isRunning());
    EXPECT_TRUE(timer2.isRunning());

    timer1.invalidate();
    EXPECT_FALSE(timer1.isRunning());
    EXPECT_TRUE(timer2.isRunning());
}

TEST_F(TimerTestFixture, UpdateTimeRefreshesNowValue) {
    SimpleTimer timer;
    timer.setTimer(1000);

    uint32_t now1 = timer.now;

    // Wait a bit
    auto start = clock->milliseconds();
    while (clock->milliseconds() - start < 10) {
        // Wait 10ms
    }

    timer.updateTime();
    uint32_t now2 = timer.now;

    EXPECT_GT(now2, now1);
}

// ============================================
// OVERFLOW HANDLING TESTS
// ============================================

/*
 * These tests verify that SimpleTimer correctly handles uint32_t overflow.
 * On ESP32, millis() wraps to 0 after ~49.7 days. The timer must continue
 * working correctly across this boundary.
 */

class TimerOverflowTest : public ::testing::Test {
protected:
    class MockOverflowClock : public PlatformClock {
    private:
        uint32_t currentTime = 0;

    public:
        void setTime(uint32_t time) {
            currentTime = time;
        }

        void advance(uint32_t ms) {
            // Allow overflow naturally
            currentTime += ms;
        }

        unsigned long milliseconds() override {
            return currentTime;
        }
    };

    std::unique_ptr<MockOverflowClock> mockClock;

    void SetUp() override {
        mockClock = std::make_unique<MockOverflowClock>();
        SimpleTimer::setPlatformClock(mockClock.get());
    }

    void TearDown() override {
        SimpleTimer::resetClock();
    }
};

TEST_F(TimerOverflowTest, ElapsedTimeCorrectAcrossOverflow) {
    // Set clock near max value
    mockClock->setTime(UINT32_MAX - 100);

    SimpleTimer timer;
    timer.setTimer(10000);

    // Advance past overflow
    mockClock->advance(200);  // Now at UINT32_MAX + 100 = 99 (wrapped)

    unsigned long elapsed = timer.getElapsedTime();
    EXPECT_EQ(elapsed, 200);
}

TEST_F(TimerOverflowTest, ExpirationCorrectAcrossOverflow) {
    // Set clock near max value
    mockClock->setTime(UINT32_MAX - 50);

    SimpleTimer timer;
    timer.setTimer(100);  // Should expire after 100ms

    // Not expired yet
    EXPECT_FALSE(timer.expired());

    // Advance past overflow but not past expiration
    mockClock->advance(80);  // Total elapsed: 80ms
    EXPECT_FALSE(timer.expired());

    // Advance to expiration
    mockClock->advance(30);  // Total elapsed: 110ms
    EXPECT_TRUE(timer.expired());
}

TEST_F(TimerOverflowTest, ExactOverflowBoundary) {
    // Start exactly at max value
    mockClock->setTime(UINT32_MAX);

    SimpleTimer timer;
    timer.setTimer(50);

    // Advance by 1 (wraps to 0)
    mockClock->advance(1);
    EXPECT_EQ(timer.getElapsedTime(), 1);
    EXPECT_FALSE(timer.expired());

    // Advance to expiration
    mockClock->advance(60);  // Total: 61ms
    EXPECT_TRUE(timer.expired());
}

TEST_F(TimerOverflowTest, LongDurationAcrossOverflow) {
    mockClock->setTime(UINT32_MAX - 1000);

    SimpleTimer timer;
    timer.setTimer(5000);

    mockClock->advance(2000);  // Crosses overflow
    EXPECT_FALSE(timer.expired());

    mockClock->advance(4000);  // Total: 6000ms
    EXPECT_TRUE(timer.expired());
}

// ============================================
// CLOCK LIFECYCLE TESTS
// ============================================

TEST(TimerClockTests, CanResetAndSetClockMultipleTimes) {
    NativeClockDriver clock1("clock1"), clock2("clock2");

    SimpleTimer::setPlatformClock(&clock1);
    EXPECT_EQ(SimpleTimer::getPlatformClock(), &clock1);

    SimpleTimer::resetClock();
    EXPECT_EQ(SimpleTimer::getPlatformClock(), nullptr);

    SimpleTimer::setPlatformClock(&clock2);
    EXPECT_EQ(SimpleTimer::getPlatformClock(), &clock2);

    SimpleTimer::resetClock();
}

TEST(TimerClockTests, NullClockHandledSafely) {
    SimpleTimer::resetClock();  // Ensure null

    SimpleTimer timer;
    timer.setTimer(1000);

    EXPECT_EQ(timer.getElapsedTime(), 0);
    EXPECT_FALSE(timer.expired());
}
