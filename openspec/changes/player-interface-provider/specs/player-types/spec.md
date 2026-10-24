## ADDED Requirements

### Requirement: Core registration types
The firmware core library SHALL define `PlayerV2`, `Team`, and `Affiliation` in `game/player-types.hpp`, suitable for use from both native tests and embedded builds. Ids SHALL be held as `UUIDBytes` and strings in fixed-size buffers sized by `PlayerLimits`.

#### Scenario: Team shape
- **WHEN** code sets a `Team` id and name `Hunters`
- **THEN** both are readable from the value type

#### Scenario: Affiliation shape
- **WHEN** code sets an `Affiliation` with category name `Allegiance` and affiliation name `Helix`
- **THEN** category and affiliation ids and names are readable from the value type

#### Scenario: Team presence
- **WHEN** a `PlayerV2` has a non-nil `team.id`
- **THEN** `hasTeam()` returns true
- **AND** a nil `team.id` makes `hasTeam()` return false

### Requirement: alleycat-server decodes into PlayerV2
`DevicePlayerDecoder` and `PlayerApi::getDevicePlayerByPin` SHALL produce `PlayerV2` without changing protobuf definitions or regenerating nanopb outputs. `PlayerLimits` buffer sizes SHALL match `device_api.options`, enforced at compile time.

#### Scenario: Decoded player
- **WHEN** `DevicePlayerDecoder` decodes a valid `DevicePlayerResponse` body
- **THEN** the resulting `PlayerV2` holds the id, event id, PIN, name, team, affiliations, and active flag from the message

### Requirement: PlayerV2 is not gameplay Player
Registration code SHALL treat `PlayerV2` as distinct from the gameplay `Player` class. Gameplay `Player` MAY carry a deprecation note pointing to `PlayerV2` for server registration reads.

#### Scenario: Type separation
- **WHEN** PlayerFetch holds a `PlayerV2`
- **THEN** it obtains PIN and RNG seed from gameplay `Player` (`getUserID()`)
- **AND** it obtains the server UUID from `PlayerV2::id`
