#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t TASK_NAME_LENGTH = 16;
constexpr size_t COMMIT_HASH_LENGTH = 9;
constexpr uint8_t MAX_CRASH_ENTRIES = 10;

/**
 * One captured crash, as the crash logger persists it. CrashEncoder maps it onto
 * the server's WriteDeviceLogRequest.
 */
struct CrashRecord {
    // Monotonic crash count for this device (1 = first crash ever recorded).
    uint32_t crashNumber;

    // Milliseconds the crashed boot had run, as of its last loop() pass. 0 when
    // unknown: a crash before the first pass, or a reset that cleared RTC memory.
    uint32_t uptimeMs;

    uint8_t resetReason;
    // Whether the reboot left a core dump summary. programCounter,
    // exceptionCause and taskName come from it and mean nothing without it.
    bool hasCoreDumpSummary;
    uint32_t programCounter;
    uint32_t exceptionCause;
    char taskName[TASK_NAME_LENGTH];
    char commitHash[COMMIT_HASH_LENGTH];
};
