## ADDED Requirements

### Requirement: Queue stores QuickdrawMatchRecord only
`MatchRecordQueue` SHALL persist a sequential list of `QuickdrawMatchRecord` values for later upload. It SHALL NOT read or write legacy JSON `Match` objects or call `Match::toJson()`.

#### Scenario: Append one record
- **WHEN** append succeeds with a valid record
- **THEN** `getCount()` increases by one
- **AND** `readAt(index)` returns equal field values

### Requirement: Fixed binary encoding
Each stored record SHALL use a fixed-size binary layout derived from `QuickdrawMatchRecord` fields (UUID bytes and integer times). Encoding SHALL be stable across firmware versions in this change.

#### Scenario: Round trip
- **WHEN** a record with non-nil team ids and zero draw times is appended and read back
- **THEN** all fields match the appended value

### Requirement: Namespace isolation
The queue SHALL use a dedicated preferences namespace distinct from legacy `matches` JSON storage.

#### Scenario: Legacy untouched
- **WHEN** the event queue stores records
- **THEN** legacy `MatchManager` prefs namespace is not modified

### Requirement: Capacity limit
Append SHALL fail when the queue reaches `MAX_STORED_EVENT_DUEL_RECORDS` (implementation constant; default **100** to align with server batch size unless design open question changes).

#### Scenario: Queue full
- **WHEN** append is called at capacity
- **THEN** append returns false
- **AND** count is unchanged

### Requirement: Clear and count
The queue SHALL support `clear()` removing all records and count zero, and `getCount()` for upload states.

#### Scenario: Clear after uploads
- **WHEN** clear is invoked with three stored records
- **THEN** getCount is zero
