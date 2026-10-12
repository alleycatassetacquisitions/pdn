#pragma once

#include <cstddef>
#include <cstdint>

#include "alleycat-server/device_api.pb.h"
#include "device/crash/crash-record.hpp"

/**
 * Encodes a CrashRecord as the WriteDeviceLogRequest protobuf that
 * POST /device-logs takes.
 */
namespace CrashEncoder {

/** Largest encoding encode() can produce. */
inline constexpr size_t MAX_ENCODED_SIZE = alleycat_device_WriteDeviceLogRequest_size;

/**
 * Encodes `record` into `outBuf`. `mac` is the device's 6-byte MAC, or nullptr
 * to omit it. Returns the bytes written, or 0 if `outBuf` is too small.
 */
size_t encode(const CrashRecord& record, const uint8_t* mac, uint8_t* outBuf, size_t bufSize);

}  // namespace CrashEncoder
