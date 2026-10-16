#pragma once

#include "device/crash/crash-record.hpp"
#include <cstddef>
#include <cstdint>

/**
 * Encodes a CrashRecord as a nanopb WriteDeviceLogRequest protobuf.
 *
 * Pure function with no platform dependencies — testable on native.
 */
namespace CrashEncoder {

/** Maximum number of bytes pb_encode can write for WriteDeviceLogRequest. */
static constexpr size_t MAX_ENCODED_SIZE = 92;

/**
 * Encodes @p record into @p outBuf as a WriteDeviceLogRequest protobuf.
 *
 * @param record   Crash record to encode.
 * @param mac      6-byte device MAC address (may be nullptr — field omitted).
 * @param outBuf   Caller-supplied buffer; must be >= MAX_ENCODED_SIZE bytes.
 * @param bufSize  Size of @p outBuf.
 * @return         Number of bytes written, or 0 on encode failure.
 */
size_t encode(const CrashRecord& record, const uint8_t* mac,
              uint8_t* outBuf, size_t bufSize);

} // namespace CrashEncoder
