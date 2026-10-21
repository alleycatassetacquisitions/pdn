## ADDED Requirements

### Requirement: PlayerProvider optional profile
Core SHALL define a `PlayerProvider` abstraction that owns zero or one registration profile and exposes whether a profile is present.

#### Scenario: Empty provider
- **WHEN** a provider has been constructed and no profile was set
- **THEN** `hasPlayer()` returns false

#### Scenario: Profile set
- **WHEN** a profile is stored in the provider
- **THEN** `hasPlayer()` returns true

### Requirement: Const access through PlayerInterface
`PlayerProvider` SHALL expose the current registration view as `const PlayerInterface&` from `getPlayer()`. When a profile is present, that reference SHALL refer to the stored profile. When no profile is present, `getPlayer()` SHALL return a stable default `PlayerInterface` (stopgap for happy-path callers), SHALL emit `LOG_E`, and SHALL NOT assert or crash.

#### Scenario: Read stored profile
- **WHEN** the provider has a stored profile with name `Ada`
- **THEN** `getPlayer().getName()` returns `Ada`

#### Scenario: No profile access
- **WHEN** `hasPlayer()` is false and a caller invokes `getPlayer()`
- **THEN** the provider returns a default player object
- **AND** logs an error with `LOG_E`
- **AND** execution continues without assert or crash

### Requirement: Set and clear profile
`PlayerProvider` SHALL support replacing the stored profile from any `PlayerInterface` implementation and clearing stored state. `setPlayer` SHALL copy only interface fields (player id, name, team, affiliations, active) into stored concrete state and SHALL NOT require retaining wire metadata on the stored object.

#### Scenario: Set from interface
- **WHEN** `setPlayer` is called with a `PlayerInterface` whose player id is `uuid-2` and name is `Bo`
- **THEN** subsequent `getPlayer()` reflects `uuid-2` and `Bo`
- **AND** team, affiliations, and active state match the source interface

#### Scenario: Clear after fetch failure or new session
- **WHEN** `clear()` is called on a provider that had a profile
- **THEN** `hasPlayer()` returns false

#### Scenario: Overwrite profile
- **WHEN** a provider already holds a profile and `setPlayer` is called with a different profile
- **THEN** the previous profile is fully replaced with no merge of team or affiliations

### Requirement: Concrete storage uses DevicePlayer
The default concrete `PlayerProvider` implementation used on device (including the migrated PlayerFetch session) SHALL store a `DevicePlayer` by value as the profile backing object.

#### Scenario: Fetch success path
- **WHEN** device player lookup succeeds and the callback receives a `DevicePlayer`
- **THEN** the session provider stores the profile via `setPlayer` (interface copy)
- **AND** later serves it via `getPlayer()` as `PlayerInterface`
