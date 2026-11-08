## MODIFIED Requirements

### Requirement: System clock update on HTTP Date header

Whenever the device receives an HTTP response header whose key matches `"Date"` (case-insensitive) and whose value passes `IMF-fixdate parsing`, it SHALL set the system UTC wall clock to the parsed Unix epoch seconds (with zero sub-second offset).

A response without a `Date` header, or with a `Date` header that fails `IMF-fixdate parsing`, SHALL leave the system wall clock unchanged.

#### Scenario: Lookup response syncs the FDN clock
- **WHEN** the FDN receives an HTTP response containing `Date: Sun, 18 Oct 2026 21:30:00 GMT`
- **THEN** the system UTC wall clock is set to `1792359000` seconds (`1792359000000` ms)

#### Scenario: Case-insensitive header key
- **WHEN** an HTTP/2 or proxy response delivers the header key as `"date"` or `"DATE"`
- **THEN** the device still parses the value and updates the system UTC wall clock

#### Scenario: Malformed Date header leaves clock untouched
- **WHEN** an HTTP response carries `Date: garbage`
- **THEN** the system UTC wall clock is not updated and the existing clock value is preserved

## ADDED Requirements

### Requirement: PDN wall clock from its FDN

The PDN has no HTTP client. When it applies a provisioning profile, it SHALL set
its UTC wall clock to the FDN wall-clock time carried in that reply, unless that
time is before `2025-01-01T00:00:00Z`, in which case its wall clock SHALL be
left unchanged. Setting the wall clock SHALL NOT alter the monotonic clock.

#### Scenario: Provisioned PDN takes the FDN's time
- **WHEN** a PDN applies a profile reply stamped `1792359000000` ms
- **THEN** its UTC wall clock reads `1792359000000` ms

#### Scenario: Unsynced FDN
- **WHEN** a PDN applies a profile reply stamped `5000` ms
- **THEN** its UTC wall clock is unchanged
