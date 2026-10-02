#include "alleycat-server/crash-encoder.hpp"
#include "alleycat-server/device_api.pb.h"
#include <pb_encode.h>
#include <cstdio>
#include <cstring>

namespace CrashEncoder {

size_t encode(const CrashRecord& record, const uint8_t* mac,
              uint8_t* outBuf, size_t bufSize) {
    alleycat_device_WriteDeviceLogRequest msg =
        alleycat_device_WriteDeviceLogRequest_init_zero;

    // MAC: 6 raw bytes → "aa:bb:cc:dd:ee:ff"
    if (mac != nullptr) {
        snprintf(msg.device_mac, sizeof(msg.device_mac),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        msg.has_device_mac = true;
    }

    msg.has_crash_number = true;
    msg.crash_number     = record.crashNumber;

    // timestamp is uptime_ms — millis() at the moment of reboot detection.
    msg.has_uptime_ms = true;
    msg.uptime_ms     = record.timestamp;

    // commitHash maps to software_version.
    if (record.commitHash[0] != '\0') {
        strncpy(msg.software_version, record.commitHash,
                sizeof(msg.software_version) - 1);
        msg.software_version[sizeof(msg.software_version) - 1] = '\0';
        msg.has_software_version = true;
    }

    msg.has_reset_reason = true;
    msg.reset_reason     = record.resetReason;

    // Omit program_counter and exception_cause when the core dump wasn't available.
    msg.has_program_counter = (record.programCounter != 0);
    msg.program_counter     = record.programCounter;

    msg.has_exception_cause = (record.exceptionCause != 0);
    msg.exception_cause     = record.exceptionCause;

    if (record.taskName[0] != '\0') {
        strncpy(msg.task_name, record.taskName, sizeof(msg.task_name) - 1);
        msg.task_name[sizeof(msg.task_name) - 1] = '\0';
        msg.has_task_name = true;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(outBuf, bufSize);
    if (!pb_encode(&stream, alleycat_device_WriteDeviceLogRequest_fields, &msg)) {
        return 0;
    }
    return stream.bytes_written;
}

} // namespace CrashEncoder
