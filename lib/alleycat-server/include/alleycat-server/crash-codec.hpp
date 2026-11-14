#pragma once

#include "alleycat-server/fdn-stored-entry.hpp"
#include <cstddef>
#include <cstdint>

/**
 * Encodes a crash record as a nanopb WriteDeviceLogRequest protobuf.
 *
 * Pure function with no platform dependencies — testable on native.
 */
namespace CrashCodec {

/** Maximum number of bytes pb_encode can write for WriteDeviceLogRequest. */
static constexpr size_t MAX_ENCODED_SIZE = 92;

/**
 * Encodes the crash record in @p entry as a WriteDeviceLogRequest under the
 * entry's sender MAC address; a byte of the task name or commit hash that is
 * not printable ASCII becomes '?'.
 *
 * @param entry    The crash record and the MAC address of the device it is from.
 * @param outBuf   Caller-supplied buffer; must be >= MAX_ENCODED_SIZE bytes.
 * @param bufSize  Size of @p outBuf.
 * @return         Number of bytes written, or 0 on encode failure.
 */
size_t encode(const FdnStoredCrashEntry& entry, uint8_t* outBuf, size_t bufSize);

}  // namespace CrashCodec
