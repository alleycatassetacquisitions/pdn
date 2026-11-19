#pragma once

#include <cstdint>

constexpr int64_t MIN_VALID_UTC_UNIX_MS = 1735689600000LL;
constexpr int64_t MAX_VALID_UTC_UNIX_MS = 253402300799999LL;

/** True when `unixMs` falls within [2025-01-01T00:00:00Z, 9999-12-31T23:59:59.999Z]. */
inline constexpr bool isValidUtcUnixMs(int64_t unixMs) {
    return unixMs >= MIN_VALID_UTC_UNIX_MS && unixMs <= MAX_VALID_UTC_UNIX_MS;
}
