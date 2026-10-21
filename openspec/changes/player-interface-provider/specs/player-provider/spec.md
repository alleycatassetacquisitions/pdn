## ADDED Requirements

### Requirement: PlayerProvider optional profile
Core SHALL define a concrete `PlayerProvider` that owns zero or one `PlayerV2` and exposes whether one is present.

#### Scenario: Empty provider
- **WHEN** a provider has been constructed and no profile was set
- **THEN** `hasPlayer()` returns false

#### Scenario: Profile set
- **WHEN** a profile is stored in the provider
- **THEN** `hasPlayer()` returns true

### Requirement: Const access
`PlayerProvider::getPlayer()` SHALL return `const PlayerV2&`. When a profile is present, it SHALL refer to the stored profile. When none is present, it SHALL return a stable empty `PlayerV2`, SHALL emit `LOG_E`, and SHALL NOT assert or crash.

#### Scenario: Read stored profile
- **WHEN** the provider holds a profile with name `Ada`
- **THEN** `getPlayer().name` is `Ada`

#### Scenario: No profile access
- **WHEN** `hasPlayer()` is false and a caller invokes `getPlayer()`
- **THEN** the provider returns an empty profile
- **AND** logs an error with `LOG_E`
- **AND** execution continues without assert or crash

### Requirement: Set and clear profile
`PlayerProvider` SHALL support replacing the stored profile and clearing it.

#### Scenario: Overwrite profile
- **WHEN** a provider already holds a profile and `setPlayer` is called with a different profile
- **THEN** the previous profile is fully replaced with no merge of team or affiliations

#### Scenario: Clear
- **WHEN** `clear()` is called on a provider that had a profile
- **THEN** `hasPlayer()` returns false

### Requirement: PlayerFetch uses PlayerProvider
`PlayerFetchApp` SHALL own a `PlayerProvider` and its states SHALL read and write the registration profile through `PlayerProvider*`.

#### Scenario: Fetch success path
- **WHEN** device player lookup succeeds
- **THEN** fetch user stores the decoded `PlayerV2` via `setPlayer`
- **AND** welcome reads the name via `getPlayer()`
