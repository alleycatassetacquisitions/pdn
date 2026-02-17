#pragma once

#include <cstdint>
#include "device/drivers/platform-clock.hpp"
#include "device/drivers/logger.hpp"

class SimpleTimer {
public:
    SimpleTimer() = default;

    static void setPlatformClock(PlatformClock* platformClock) {
        clock = platformClock;
    }

    static PlatformClock* getPlatformClock() {
        return clock;
    }

    /**
     * Reset the static clock to nullptr.
     * Must be called between tests to prevent state pollution.
     */
    static void resetClock() {
        clock = nullptr;
    }

    void updateTime(){
        if (clock == nullptr) {
            now = 0;
            return;
        }
        // Cast to uint32_t to simulate ESP32's 32-bit millis() behavior
        // This ensures overflow handling works correctly on 64-bit test platforms
        now = static_cast<uint32_t>(clock->milliseconds());
    }
    
    /*
     * Returns elapsed time since timer start in milliseconds.
     *
     * Overflow safety: This method is safe across uint32_t overflow boundaries.
     * On ESP32, millis() wraps to 0 after ~49.7 days. When overflow occurs,
     * unsigned subtraction (now - start) correctly handles the wraparound due to
     * modular arithmetic in C++. For example:
     *   - start = 0xFFFFFFFE (near max)
     *   - now   = 0x00000005 (wrapped past 0)
     *   - now - start = 0x00000005 - 0xFFFFFFFE = 0x00000007 = 7 (correct!)
     *
     * This works because both operands are the same unsigned type (uint32_t).
     */
    unsigned long getElapsedTime()
    {
        if (clock == nullptr) {
            return 0;
        }
        uint32_t currentTime = static_cast<uint32_t>(clock->milliseconds());
        return currentTime - start;
    }
    
    /*
     * Checks if the timer has expired.
     *
     * Overflow safety: This method is safe across uint32_t overflow boundaries
     * because getElapsedTime() correctly handles unsigned wraparound (see above).
     * The comparison (duration <= elapsed) will work correctly even if millis()
     * has wrapped around zero since the timer was started.
     */
    bool expired()
    {
        if(running) {
            return duration <= getElapsedTime();
        }

        return false;
    }
    
    bool isRunning() {
        return running;
    }
    
    void invalidate()
    {
        duration = 0;
        running = false;
    }
    
    void setTimer(unsigned long timerDelay)
    {
        running = true;
        // Cast to uint32_t to match internal storage and ESP32 behavior
        duration = static_cast<uint32_t>(timerDelay);
        updateTime();
        start = now;
    }

    // Use uint32_t to match ESP32's millis() which is 32-bit and wraps at UINT32_MAX
    // This ensures correct overflow handling on both ESP32 and 64-bit test platforms
    uint32_t now = 0;

private:

    bool running = false;
    uint32_t start = 0;
    uint32_t duration = 0;
    static PlatformClock* clock;
};
