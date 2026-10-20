## ADDED Requirements

### Requirement: Core registration value types
The firmware core library SHALL define registration value types in `game/player-types.hpp` suitable for use from both native tests and embedded builds.

#### Scenario: Team assignment shape
- **WHEN** code constructs a `PlayerTeamAssignment` with team id `t-1` and name `Hunters`
- **THEN** both identifiers are readable from the value type

#### Scenario: Affiliation shape
- **WHEN** code constructs a `PlayerAffiliation` with category name `Allegiance` and affiliation name `Helix`
- **THEN** category and affiliation ids and names are readable from the value type

### Requirement: PlayerInterface read contract
Core SHALL define an abstract `PlayerInterface` type that exposes read-only registration fields: player id, name, teams, affiliations, and active state.

#### Scenario: Full profile
- **WHEN** an implementation has player id `uuid-1`, name `Ada`, one team, two affiliations, and `active` true
- **THEN** `getPlayerId()` returns `uuid-1`
- **AND** `getName()` returns `Ada`
- **AND** `getTeams()` returns a collection of size one with that team
- **AND** `getAffiliations()` returns a collection of size two
- **AND** `isActive()` returns true

#### Scenario: Unassigned team
- **WHEN** an implementation has no team assignment
- **THEN** `getTeams()` returns an empty collection

### Requirement: DevicePlayer implements PlayerInterface
The alleycat-server `DevicePlayer` type SHALL publicly inherit `PlayerInterface` and SHALL implement all interface methods from its stored fields without changing protobuf definitions or regenerating nanopb outputs.

#### Scenario: Decoded player maps to interface
- **WHEN** `DevicePlayerDecoder` decodes a valid `DevicePlayerResponse` body into `DevicePlayer`
- **THEN** the resulting object can be used as a `const PlayerInterface&`
- **AND** id, name, team fields, affiliations, and active match the decoded message

#### Scenario: Single server team exposed as teams collection
- **WHEN** a `DevicePlayer` has non-empty `teamId` and `teamName`
- **THEN** `getTeams()` returns exactly one `PlayerTeamAssignment` with those values

#### Scenario: No team on wire
- **WHEN** a `DevicePlayer` has empty `teamId`
- **THEN** `getTeams()` returns an empty collection

### Requirement: Interface is not gameplay Player
Registration code SHALL treat `PlayerInterface` as distinct from the concrete gameplay `Player` class. `PlayerInterface` SHALL NOT be required to expose PIN, stats, hunter role, or legacy allegiance enum fields.

#### Scenario: Type separation
- **WHEN** PlayerFetch or tests hold a `PlayerInterface` reference
- **THEN** they obtain PIN and RNG seed from gameplay `Player` separately (e.g. `getUserID()`)
- **AND** they obtain server UUID from `getPlayerId()` on the interface
