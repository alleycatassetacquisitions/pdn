## ADDED Requirements

### Requirement: Core registration value types
The firmware core library SHALL define registration value types in `game/player-types.hpp` suitable for use from both native tests and embedded builds.

#### Scenario: Team shape
- **WHEN** code constructs a `Team` with team id `t-1` and name `Hunters`
- **THEN** both identifiers are readable from the value type

#### Scenario: Affiliation shape
- **WHEN** code constructs an `Affiliation` with category name `Allegiance` and affiliation name `Helix`
- **THEN** category and affiliation ids and names are readable from the value type

### Requirement: PlayerInterface read contract
Core SHALL define an abstract `PlayerInterface` type that exposes read-only registration fields: player id, name, team, affiliations, and active state. The interface SHALL include only fields the app layer needs; wire-only metadata on `DevicePlayer` (event id, PDN code, created timestamp) is intentionally excluded and is not retained when copying through `PlayerProvider::setPlayer`.

#### Scenario: Full profile
- **WHEN** an implementation has player id `uuid-1`, name `Ada`, a team, two affiliations, and `active` true
- **THEN** `getPlayerId()` returns `uuid-1`
- **AND** `getName()` returns `Ada`
- **AND** `getTeam()` returns that team
- **AND** `getAffiliations()` returns a collection of size two
- **AND** `isActive()` returns true

#### Scenario: Team present on profile
- **WHEN** a registration profile is stored for app use (fetch success or demo stub with team fields)
- **THEN** `getTeam()` returns the assigned team (every player is assumed to have at least one team for happy-path flows)

### Requirement: DevicePlayer implements PlayerInterface
The alleycat-server `DevicePlayer` type SHALL publicly inherit `PlayerInterface` and SHALL implement all interface methods from its stored fields without changing protobuf definitions or regenerating nanopb outputs.

#### Scenario: Decoded player maps to interface
- **WHEN** `DevicePlayerDecoder` decodes a valid `DevicePlayerResponse` body into `DevicePlayer`
- **THEN** the resulting object can be used as a `const PlayerInterface&`
- **AND** id, name, team fields, affiliations, and active match the decoded message for interface accessors

#### Scenario: Single server team exposed as Team
- **WHEN** a `DevicePlayer` has non-empty `teamId` and `teamName`
- **THEN** `getTeam()` returns a `Team` with those values

### Requirement: Interface is not gameplay Player
Registration code SHALL treat `PlayerInterface` as distinct from the concrete gameplay `Player` class. `PlayerInterface` SHALL NOT be required to expose PIN, stats, hunter role, or legacy allegiance enum fields. Gameplay `Player` MAY carry a deprecation note pointing to `PlayerInterface` for server registration reads.

#### Scenario: Type separation
- **WHEN** PlayerFetch or tests hold a `PlayerInterface` reference
- **THEN** they obtain PIN and RNG seed from gameplay `Player` separately (e.g. `getUserID()`)
- **AND** they obtain server UUID from `getPlayerId()` on the interface
