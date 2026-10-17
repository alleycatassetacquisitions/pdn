#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>

constexpr size_t  TASK_NAME_LENGTH   = 16;
constexpr size_t  COMMIT_HASH_LENGTH = 9;
constexpr uint8_t MAX_CRASH_ENTRIES  = 10;

/**
 * The crashed task, PC and exception cause from a core dump summary.
 */
struct CoreDumpDetails {
    uint32_t programCounter;
    /** Empty after a task watchdog reset, whose saved frame holds no exception cause. */
    std::optional<uint32_t> exceptionCause;
    char taskName[TASK_NAME_LENGTH];
};

/**
 * Canonical in-memory representation of a captured crash event.
 *
 * Written by CrashLogger on boot after inspecting the reset reason and core
 * dump summary. Read by the Alleycat server encoder for HTTP upload.
 */
struct CrashRecord {
    /** Monotonic crash count for this device (1 = first crash ever recorded). */
    uint32_t crashNumber;

    /** Milliseconds since boot at the moment the crash is detected on reboot. */
    uint32_t timestamp;

    uint8_t  resetReason;
    /** Empty unless capture read usable details from a core dump summary. */
    std::optional<CoreDumpDetails> coreDumpDetails;
    char     commitHash[COMMIT_HASH_LENGTH];
};
