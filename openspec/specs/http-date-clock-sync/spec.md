# http-date-clock-sync Specification

## Purpose

Set the ESP32-S3 POSIX wall clock from the `Date` header of HTTP responses from `alleycat-rs`, and expose UTC epoch milliseconds on `PlatformClock` without disturbing the monotonic millisecond clock used by timers and gameplay.

## Requirements

### Requirement: IMF-fixdate parsing

The firmware SHALL parse HTTP `Date` header values formatted as RFC 7231 `IMF-fixdate` (`<day-name>, DD Mon YYYY HH:MM:SS GMT`) into Unix epoch seconds (`int64_t`) in UTC.

The parser SHALL accept a date only when:

- the format matches `Www, DD Mon YYYY HH:MM:SS GMT` (with `DD` `01`–`31` valid for that month and leap-year rules applied to February, `HH` `00`–`23`, `MM` `00`–`59`, `SS` `00`–`60`);
- the resulting timestamp is at least `2025-01-01T00:00:00Z` (`1735689600`) and at most `9999-12-31T23:59:59Z` (`253402300799`).

Any malformed, empty, non-GMT, or out-of-range value SHALL be rejected without side effects.

#### Scenario: Valid HTTP Date header
- **WHEN** the parser is given `"Sun, 18 Oct 2026 21:30:00 GMT"`
- **THEN** it returns `1792359000` Unix epoch seconds

#### Scenario: Leap-day handling
- **WHEN** the parser is given `"Tue, 29 Feb 2028 00:00:00 GMT"`
- **THEN** it accepts the date and returns `1835395200`
- **WHEN** the parser is given `"Sun, 29 Feb 2026 00:00:00 GMT"`
- **THEN** it rejects the date because 2026 is not a leap year

#### Scenario: Pre-2025 or malformed header rejected
- **WHEN** the parser is given `"Thu, 01 Jan 1970 00:00:00 GMT"` or `"invalid-date"`
- **THEN** it rejects the value

### Requirement: System clock update on HTTP Date header

Whenever `Esp32S3HttpClient` receives an HTTP response header whose key matches `"Date"` (case-insensitive) and whose value passes `IMF-fixdate parsing`, it SHALL set the system POSIX clock via `settimeofday` to `{tv_sec = parsed_seconds, tv_usec = 0}`.

A response without a `Date` header, or with a `Date` header that fails `IMF-fixdate parsing`, SHALL leave the system clock unchanged.

#### Scenario: Registration response syncs the clock
- **WHEN** the PDN receives a response from `alleycat-rs` containing `Date: Sun, 18 Oct 2026 21:30:00 GMT`
- **THEN** `settimeofday` sets the ESP32 POSIX clock to `1792359000` seconds (`1792359000000` ms)

#### Scenario: Case-insensitive header key
- **WHEN** an HTTP/2 or proxy response delivers the header key as `"date"` or `"DATE"`
- **THEN** the driver still parses the value and updates the system clock

#### Scenario: Malformed Date header leaves clock untouched
- **WHEN** an HTTP response carries `Date: garbage`
- **THEN** `settimeofday` is not called and the existing clock value is preserved

### Requirement: Monotonic timer isolation

Updating the POSIX wall clock via `settimeofday` SHALL NOT alter `PlatformClock::milliseconds()`.

- `PlatformClock::milliseconds()` SHALL continue to return monotonic milliseconds since boot (`esp_timer_get_time() / 1000` on ESP32-S3), used by `SimpleTimer`, HTTP request timeouts, and duel draw-time measurements.
- `PlatformClock::unixMilliseconds()` SHALL return `int64_t` UTC milliseconds since `1970-01-01T00:00:00Z` (`gettimeofday` on ESP32-S3, `std::chrono::system_clock` on native).

#### Scenario: In-flight HTTP request does not time out when Date header arrives
- **WHEN** an HTTP request starts when `milliseconds()` is `5000` and receives `Date: Sun, 18 Oct 2026 21:30:00 GMT` during header reception
- **THEN** `unixMilliseconds()` jumps to `1792359000000`, while `milliseconds()` remains ~`5000` and the in-flight request does not spuriously expire `REQUEST_TIMEOUT_MS`
