#pragma once

#include <cstdint>
#include <cstddef>

constexpr size_t  TASK_NAME_LENGTH   = 16;
constexpr size_t  COMMIT_HASH_LENGTH = 9;
constexpr uint8_t MAX_CRASH_ENTRIES  = 10;

/**
 * Canonical in-memory representation of a captured crash event.
 *
 * Written by CrashLogger on boot after inspecting the reset reason and core
 * dump summary. Read by the Alleycat server encoder for HTTP upload.
 */
struct CrashRecord {
    /** Monotonic crash count for this device (1 = first crash ever recorded). */
    uint32_t crashNumber;

    /** millis() at the moment the crash is detected on reboot. */
    uint32_t timestamp;

    uint8_t  resetReason;
    uint32_t programCounter;
    uint32_t exceptionCause;
    char     taskName[TASK_NAME_LENGTH];
    char     commitHash[COMMIT_HASH_LENGTH];
};
