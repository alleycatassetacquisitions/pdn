#include "alleycat-server/crash-encoder.hpp"

#include <pb_encode.h>

#include <cstdio>
#include <cstring>

namespace CrashEncoder {

static_assert(sizeof(alleycat_device_WriteDeviceLogRequest::task_name) >= TASK_NAME_LENGTH,
              "device_api.options must give task_name room for CrashRecord::taskName");
static_assert(sizeof(alleycat_device_WriteDeviceLogRequest::software_version) >= COMMIT_HASH_LENGTH,
              "device_api.options must give software_version room for CrashRecord::commitHash");

size_t encode(const CrashRecord& record, const uint8_t* mac, uint8_t* outBuf, size_t bufSize) {
    alleycat_device_WriteDeviceLogRequest msg = alleycat_device_WriteDeviceLogRequest_init_zero;

    if (mac != nullptr) {
        snprintf(msg.device_mac, sizeof(msg.device_mac), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
        msg.has_device_mac = true;
    }

    msg.has_crash_number = true;
    msg.crash_number = record.crashNumber;

    msg.has_uptime_ms = true;
    msg.uptime_ms = record.timestamp;

    if (record.commitHash[0] != '\0') {
        strncpy(msg.software_version, record.commitHash, sizeof(msg.software_version) - 1);
        msg.has_software_version = true;
    }

    msg.has_reset_reason = true;
    msg.reset_reason = record.resetReason;

    // A zero program counter means the reboot left no core dump summary. The
    // exception cause follows it rather than its own value, because cause 0
    // (IllegalInstruction) is a real cause.
    const bool hadCoreDumpSummary = record.programCounter != 0;
    msg.has_program_counter = hadCoreDumpSummary;
    msg.program_counter = record.programCounter;
    msg.has_exception_cause = hadCoreDumpSummary;
    msg.exception_cause = record.exceptionCause;

    // Without a summary the logger stores the placeholder "unknown", which is not a task.
    if (hadCoreDumpSummary && record.taskName[0] != '\0') {
        strncpy(msg.task_name, record.taskName, sizeof(msg.task_name) - 1);
        msg.has_task_name = true;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(outBuf, bufSize);
    if (!pb_encode(&stream, alleycat_device_WriteDeviceLogRequest_fields, &msg)) {
        return 0;
    }
    return stream.bytes_written;
}

}  // namespace CrashEncoder
