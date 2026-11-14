#include "alleycat-server/crash-codec.hpp"
#include "alleycat-server/device_api.pb.h"
#include "device/crash/crash-record.hpp"
#include <pb_encode.h>
#include <cstdio>
#include <cstring>

namespace {

// Copies @p source, a terminated string, into @p destination of the same
// length or longer. A proto3 string must be UTF-8, and the server refuses a
// body that holds one that is not.
void copyAsPrintableAscii(char* destination, const char* source) {
    size_t index = 0;
    for (; source[index] != '\0'; index++) {
        const char character = source[index];
        destination[index] = (character >= 0x20 && character <= 0x7E) ? character : '?';
    }
    destination[index] = '\0';
}

}  // namespace

namespace CrashCodec {

static_assert(sizeof(alleycat_device_WriteDeviceLogRequest::software_version) >= COMMIT_HASH_LENGTH,
              "software_version in device_api.options must hold a commit hash");
static_assert(sizeof(alleycat_device_WriteDeviceLogRequest::task_name) >= TASK_NAME_LENGTH,
              "task_name in device_api.options must hold a task name");

size_t encode(const FdnStoredCrashEntry& entry, uint8_t* outBuf, size_t bufSize) {
    CrashPacket packet{};
    memcpy(&packet, entry.content.recordBytes, sizeof(packet));
    const CrashRecord record = fromCrashPacket(packet);

    alleycat_device_WriteDeviceLogRequest msg = alleycat_device_WriteDeviceLogRequest_init_zero;

    // MAC: 6 raw bytes → "aa:bb:cc:dd:ee:ff"
    snprintf(msg.device_mac, sizeof(msg.device_mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             entry.senderMac[0], entry.senderMac[1], entry.senderMac[2], entry.senderMac[3],
             entry.senderMac[4], entry.senderMac[5]);
    msg.has_device_mac = true;

    msg.has_crash_number = true;
    msg.crash_number = record.crashNumber;

    // timestamp maps to uptime_ms.
    msg.has_uptime_ms = true;
    msg.uptime_ms = record.timestamp;

    // commitHash maps to software_version.
    if (record.commitHash[0] != '\0') {
        copyAsPrintableAscii(msg.software_version, record.commitHash);
        msg.has_software_version = true;
    }

    msg.has_reset_reason = true;
    msg.reset_reason = record.resetReason;

    if (record.coreDumpDetails) {
        msg.has_program_counter = true;
        msg.program_counter = record.coreDumpDetails->programCounter;

        // Cause 0 is a real cause: IllegalInstruction, which abort() raises.
        if (record.coreDumpDetails->exceptionCause.has_value()) {
            msg.has_exception_cause = true;
            msg.exception_cause = *record.coreDumpDetails->exceptionCause;
        }

        copyAsPrintableAscii(msg.task_name, record.coreDumpDetails->taskName);
        msg.has_task_name = true;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(outBuf, bufSize);
    if (!pb_encode(&stream, alleycat_device_WriteDeviceLogRequest_fields, &msg)) {
        return 0;
    }
    return stream.bytes_written;
}

}  // namespace CrashCodec
