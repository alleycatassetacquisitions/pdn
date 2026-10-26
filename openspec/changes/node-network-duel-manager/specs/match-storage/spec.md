## ADDED Requirements

### Requirement: Stores pending upload records
`MatchStorage` SHALL persist `QuickdrawMatchRecord` values awaiting upload. It SHALL NOT read or write legacy JSON `Match` data.

#### Scenario: Append
- **WHEN** a record is appended successfully
- **THEN** `getCount()` increases by one
- **AND** `readAt(index)` returns a record equal field-for-field

### Requirement: Typical depth is one
The storage SHALL support multiple pending records for offline retry, but callers are expected to upload after each finalized match, so counts of zero or one are the normal case.

#### Scenario: Upload clears
- **WHEN** an upload succeeds and the caller clears storage
- **THEN** `getCount()` is zero

### Requirement: Binary encoding survives string storage
Each record SHALL be stored as a fixed-size binary layout. Because the storage interface is string-based, the encoded value SHALL be built and read by explicit length so zero bytes are preserved.

#### Scenario: Zero bytes
- **WHEN** a record with a nil team id and a zero draw time is appended and read back
- **THEN** all fields match

### Requirement: Dedicated namespace
`MatchStorage` SHALL use its own preferences namespace, distinct from the legacy `matches` namespace.

#### Scenario: Legacy untouched
- **WHEN** records are appended
- **THEN** no key in the legacy `matches` namespace is written

### Requirement: Capacity bound
Append SHALL fail when the count equals `MAX_PENDING_RECORDS` (default 100).

#### Scenario: Full
- **WHEN** append is called at capacity
- **THEN** it returns false and the count is unchanged

### Requirement: Failed write is not counted
If a write fails, the count SHALL NOT increase.

#### Scenario: Write failure
- **WHEN** the underlying write reports fewer bytes than requested
- **THEN** append returns false
- **AND** `getCount()` is unchanged
