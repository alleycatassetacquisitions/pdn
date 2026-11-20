#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
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
 * dump summary. Read by the Alleycat server codec for HTTP upload.
 */
struct CrashRecord {
    /** Monotonic crash count for this device (1 = first crash ever recorded). */
    uint32_t crashNumber;

    /** Milliseconds the crashed boot had run, as of its last loop() pass; 0 if unknown. */
    uint32_t timestamp;

    uint8_t  resetReason;
    /** Empty unless capture read usable details from a core dump summary. */
    std::optional<CoreDumpDetails> coreDumpDetails;
    char     commitHash[COMMIT_HASH_LENGTH];
};

/**
 * A crash record as it travels over ESP-NOW and sits in NVS. It has no presence
 * flags: an empty task name marks a record without core dump details, and
 * CRASH_PACKET_NO_EXCEPTION_CAUSE one with details but no exception cause.
 */
struct CrashPacket {
    uint32_t crashNumber;
    uint32_t timestamp;
    uint8_t resetReason;
    uint32_t programCounter;
    uint32_t exceptionCause;
    char taskName[TASK_NAME_LENGTH];
    char commitHash[COMMIT_HASH_LENGTH];
} __attribute__((packed));

// No real exception cause is 0xFFFF; a core dump reports it for a frame it could not read.
constexpr uint32_t CRASH_PACKET_NO_EXCEPTION_CAUSE = 0xFFFF;

/** @return @p record in the layout it is relayed in. */
inline CrashPacket toCrashPacket(const CrashRecord& record) {
    CrashPacket packet{};
    packet.crashNumber = record.crashNumber;
    packet.timestamp = record.timestamp;
    packet.resetReason = record.resetReason;
    if (record.coreDumpDetails) {
        packet.programCounter = record.coreDumpDetails->programCounter;
        packet.exceptionCause =
        record.coreDumpDetails->exceptionCause.value_or(CRASH_PACKET_NO_EXCEPTION_CAUSE);
        memcpy(packet.taskName, record.coreDumpDetails->taskName, TASK_NAME_LENGTH);
    }
    memcpy(packet.commitHash, record.commitHash, COMMIT_HASH_LENGTH);
    return packet;
}

/**
 * @return The record @p packet carries. A packet comes off the radio from any
 * PDN, so a string with no terminator is cut at its last byte.
 */
inline CrashRecord fromCrashPacket(const CrashPacket& packet) {
    CrashRecord record{};
    record.crashNumber = packet.crashNumber;
    record.timestamp = packet.timestamp;
    record.resetReason = packet.resetReason;
    // An empty task name marks a record without core dump details.
    if (packet.taskName[0] != '\0') {
        CoreDumpDetails& details = record.coreDumpDetails.emplace();
        details.programCounter = packet.programCounter;
        // Copied out: optional's assignment takes a reference, and the packed field is misaligned.
        const uint32_t exceptionCause = packet.exceptionCause;
        if (exceptionCause != CRASH_PACKET_NO_EXCEPTION_CAUSE) {
            details.exceptionCause = exceptionCause;
        }
        memcpy(details.taskName, packet.taskName, TASK_NAME_LENGTH - 1);
        details.taskName[TASK_NAME_LENGTH - 1] = '\0';
    }
    memcpy(record.commitHash, packet.commitHash, COMMIT_HASH_LENGTH - 1);
    record.commitHash[COMMIT_HASH_LENGTH - 1] = '\0';
    return record;
}
