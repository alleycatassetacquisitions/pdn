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
When a profile is present, `PlayerProvider` SHALL expose it only as `const PlayerInterface&` from `getPlayer()`.

#### Scenario: Read stored profile
- **WHEN** the provider has a stored profile with name `Ada`
- **THEN** `getPlayer().getName()` returns `Ada`

#### Scenario: No profile access
- **WHEN** `hasPlayer()` is false
- **THEN** callers MUST NOT invoke `getPlayer()` (undefined behavior avoided by app logic; tests may assert `hasPlayer()` first)

### Requirement: Set and clear profile
`PlayerProvider` SHALL support replacing the stored profile from any `PlayerInterface` implementation and clearing stored state.

#### Scenario: Set from interface
- **WHEN** `setPlayer` is called with a `PlayerInterface` whose player id is `uuid-2` and name is `Bo`
- **THEN** subsequent `getPlayer()` reflects `uuid-2` and `Bo`
- **AND** teams, affiliations, and active state match the source interface

#### Scenario: Clear after fetch failure or new session
- **WHEN** `clear()` is called on a provider that had a profile
- **THEN** `hasPlayer()` returns false

#### Scenario: Overwrite profile
- **WHEN** a provider already holds a profile and `setPlayer` is called with a different profile
- **THEN** the previous profile is fully replaced with no merge of teams or affiliations

### Requirement: Concrete storage uses DevicePlayer
The default concrete `PlayerProvider` implementation used on device (including the migrated PlayerFetch session) SHALL store a `DevicePlayer` by value as the profile backing object.

#### Scenario: Fetch success path
- **WHEN** device player lookup succeeds and the callback receives a `DevicePlayer`
- **THEN** the session provider can store that value and later serve it via `getPlayer()` as `PlayerInterface`
