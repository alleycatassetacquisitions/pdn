## ADDED Requirements

### Requirement: System clock set by SNTP

An FDN SHALL set its system UTC wall clock by SNTP from the NTP server in its build configuration (default `pool.ntp.org`), starting at its first Wi-Fi connection and asking again on a later connection while the clock is unset. A closed venue network SHALL provide an NTP server the FDN can reach.

#### Scenario: First Wi-Fi connection starts SNTP
- **WHEN** a cold-booted FDN joins Wi-Fi for its first upload cycle
- **THEN** it asks the configured NTP server for the time and sets its UTC wall clock from the answer

## MODIFIED Requirements

### Requirement: Monotonic timer isolation

Updating the UTC wall clock SHALL NOT alter the monotonic millisecond clock.

- The monotonic clock SHALL continue to report elapsed milliseconds since boot, used by protocol timers, HTTP request timeouts, and match draw-time measurements.
- The UTC wall clock SHALL report signed 64-bit UTC milliseconds since `1970-01-01T00:00:00Z`.

#### Scenario: In-flight HTTP request does not time out when Date header arrives
- **WHEN** an HTTP request starts when the monotonic clock is at `5000` ms and SNTP sets the UTC wall clock to `1792359000000` ms while the request is in flight
- **THEN** the UTC wall clock jumps to `1792359000000` ms, while the monotonic clock remains near `5000` ms and the in-flight request does not spuriously time out

## REMOVED Requirements

### Requirement: IMF-fixdate parsing

**Reason**: The FDN no longer reads the server's `Date` header, so nothing parses one.

**Migration**: The clock comes from SNTP ("System clock set by SNTP"); the 2025–9999 validity range stays for FDN-to-PDN propagation.

### Requirement: System clock update on HTTP Date header

**Reason**: Certificate dates are checked against the clock, so it must be set before the first TLS connection, not from that connection's answer.

**Migration**: "System clock set by SNTP" sets the clock; fdn-server-trust's "Clock set before the first TLS connection" holds requests until it is set.
