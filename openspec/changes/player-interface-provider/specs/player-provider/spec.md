## ADDED Requirements

### Requirement: PlayerProvider owns session Player
Core SHALL define a concrete `PlayerProvider` that owns exactly one gameplay `Player` for the device session. `PlayerProvider::getPlayer()` SHALL return a non-null `Player*` to that owned instance.

#### Scenario: Session player always exists
- **WHEN** a provider has been constructed
- **THEN** `getPlayer()` returns the same `Player` for the lifetime of the provider

### Requirement: Optional registration profile on Player
Whether a server registration profile is present SHALL be tracked on the owned `Player` via `hasRegistrationProfile()`. `PlayerProvider::hasPlayer()` SHALL return the same value as `getPlayer()->hasRegistrationProfile()`.

#### Scenario: Empty session
- **WHEN** a provider has been constructed and no registration profile was applied
- **THEN** `hasPlayer()` returns false

#### Scenario: Profile applied
- **WHEN** a registration profile is stored on the owned `Player`
- **THEN** `hasPlayer()` returns true

### Requirement: Clear registration session
`PlayerProvider::clear()` SHALL drop the registration profile on the owned `Player` and clear the stored registration PIN.

#### Scenario: Clear
- **WHEN** `clear()` is called after a profile was applied
- **THEN** `hasPlayer()` returns false
- **AND** the registration PIN is empty

### Requirement: PlayerFetch uses PlayerProvider
`PlayerFetchApp` SHALL receive `PlayerProvider*`. Its states SHALL use the provider for PIN and session clear, and SHALL read registration fields only through `playerProvider->getPlayer()` (server name, team, affiliations, server id). PlayerFetch SHALL NOT use legacy gameplay accessors (`getName()`, `getUserID()`, etc.) for registration UI.

#### Scenario: Fetch success path
- **WHEN** device player lookup succeeds
- **THEN** the decoded protobuf is applied to the owned `Player` registration fields
- **AND** welcome reads the display name via `getPlayer()->getServerPlayerName()`

#### Scenario: Main wiring
- **WHEN** firmware constructs Quickdraw and PlayerFetch
- **THEN** both receive the `Player*` from the same `PlayerProvider` instance created in `main`
