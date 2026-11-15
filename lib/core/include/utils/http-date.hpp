#pragma once

#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>

constexpr int64_t MIN_VALID_UTC_UNIX_MS = 1735689600000LL;
constexpr int64_t MAX_VALID_UTC_UNIX_MS = 253402300799999LL;

/** True when `unixMs` falls within [2025-01-01T00:00:00Z, 9999-12-31T23:59:59.999Z]. */
inline constexpr bool isValidUtcUnixMs(int64_t unixMs) {
    return unixMs >= MIN_VALID_UTC_UNIX_MS && unixMs <= MAX_VALID_UTC_UNIX_MS;
}

/** Parses RFC 7231 IMF-fixdate (e.g. "Sun, 18 Oct 2026 21:30:00 GMT") into UTC epoch seconds. */
inline std::optional<int64_t> parseHttpDate(const char* value) {
    if (value == nullptr || std::strlen(value) != 29) {
        return std::nullopt;
    }

    std::tm parsed = {};
    std::istringstream stream(value);
    stream.imbue(std::locale::classic());
    stream >> std::get_time(&parsed, "%a, %d %b %Y %H:%M:%S GMT");
    if (stream.fail() || stream.peek() != std::char_traits<char>::eof()) {
        return std::nullopt;
    }

    const int year = parsed.tm_year + 1900;
    if (year < 2025 || year > 9999) {
        return std::nullopt;
    }

    std::tm normalized = parsed;
    normalized.tm_isdst = 0;
    tzset();
    const std::time_t localEpoch = std::mktime(&normalized);
    if (localEpoch == static_cast<std::time_t>(-1) ||
        normalized.tm_year != parsed.tm_year ||
        normalized.tm_mon != parsed.tm_mon ||
        normalized.tm_mday != parsed.tm_mday) {
        return std::nullopt;
    }

#if defined(_NEWLIB_VERSION)
    return static_cast<int64_t>(localEpoch) - static_cast<int64_t>(_timezone);
#else
    return static_cast<int64_t>(localEpoch) - static_cast<int64_t>(timezone);
#endif
}
