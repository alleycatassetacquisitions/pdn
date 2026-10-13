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

    // Gated on the summary, not on the values: PC 0 (a call through a null
    // function pointer) and cause 0 (IllegalInstruction) are both real.
    if (record.hasCoreDumpSummary) {
        msg.has_program_counter = true;
        msg.program_counter = record.programCounter;
        msg.has_exception_cause = true;
        msg.exception_cause = record.exceptionCause;
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
