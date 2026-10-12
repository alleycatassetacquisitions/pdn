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

    // Milliseconds since boot at the moment the crash is detected on reboot.
    uint32_t timestamp;

    uint8_t resetReason;
    // Zero when the reboot left no core dump summary to read.
    uint32_t programCounter;
    uint32_t exceptionCause;
    char taskName[TASK_NAME_LENGTH];
    char commitHash[COMMIT_HASH_LENGTH];
};
