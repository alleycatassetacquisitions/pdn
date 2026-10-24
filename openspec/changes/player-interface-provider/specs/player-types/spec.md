## ADDED Requirements

### Requirement: Core registration types
The firmware core library SHALL define `Team`, `Affiliation`, and `PlayerLimits` in `game/player-types.hpp`, suitable for use from both native tests and embedded builds. Ids SHALL be held as `UUIDBytes` and strings in fixed-size buffers sized by `PlayerLimits`.

The server registration payload (id, name, team, affiliations) SHALL live on `Player` in `game/player.hpp`, not as a separate `PlayerV2` type.

#### Scenario: Team shape
- **WHEN** code sets a `Team` id and name `Hunters`
- **THEN** both are readable from the value type

#### Scenario: Affiliation shape
- **WHEN** code sets an `Affiliation` with category name `Allegiance` and affiliation name `Helix`
- **THEN** category and affiliation ids and names are readable from the value type

#### Scenario: Team presence on Player
- **WHEN** the owned `Player` has a non-nil registration `team.id`
- **THEN** `Player::hasTeam()` returns true
- **AND** a nil `team.id` makes `hasTeam()` return false

### Requirement: alleycat-server decodes into Player
`DevicePlayerDecoder` and `PlayerApi::getDevicePlayerByPin` SHALL decode into the registration fields on `Player` without changing protobuf definitions or regenerating nanopb outputs. `PlayerLimits` buffer sizes SHALL match `device_api.options`, enforced at compile time.

#### Scenario: Decoded player
- **WHEN** `DevicePlayerDecoder` decodes a valid `DevicePlayerResponse` body into a `Player`
- **THEN** the `Player` registration fields hold id, name, team, and affiliations from the message
- **AND** `hasRegistrationProfile()` is true

### Requirement: Legacy shim on Player
After a registration profile is applied, `Player` SHALL update legacy gameplay fields (display name, user id string, allegiance, hunter, faction) internally so existing Quickdraw call sites continue to work without changes.

#### Scenario: Affiliation maps to allegiance
- **WHEN** the first registration affiliation name contains `Helix`
- **THEN** `getAllegiance()` returns `Allegiance::HELIX`

#### Scenario: Team name maps to hunter role
- **WHEN** registration team name is `Hunter`
- **THEN** `isHunter()` returns true
- **WHEN** registration team name is `Bounty`
- **THEN** `isHunter()` returns false
