#include "device/crash/crash-logger.hpp"

#include <esp_attr.h>
#include <esp_timer.h>

namespace {

// RTC memory survives a panic restart; the marker tells a written value from the
// random contents a power-on leaves.
struct CrashedBootUptime {
    uint32_t marker;
    uint32_t uptimeMs;
};
constexpr uint32_t CRASHED_BOOT_UPTIME_MARKER = 0x55505449;
RTC_NOINIT_ATTR CrashedBootUptime crashedBootUptime;

}  // namespace

uint32_t CrashLogger::takeCrashedBootUptime() {
    const uint32_t uptimeMs =
        crashedBootUptime.marker == CRASHED_BOOT_UPTIME_MARKER ? crashedBootUptime.uptimeMs : 0;
    crashedBootUptime.marker = 0;
    return uptimeMs;
}

void CrashLogger::noteUptime() {
    crashedBootUptime.uptimeMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    crashedBootUptime.marker = CRASHED_BOOT_UPTIME_MARKER;
}
