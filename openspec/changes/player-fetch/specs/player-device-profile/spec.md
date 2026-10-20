## ADDED Requirements

### Requirement: Fetch session holds DevicePlayer
After a successful device player lookup, PlayerFetch SHALL retain the full decoded `DevicePlayer` for the current registration session and SHALL use it as the source for welcome display (name, and team/affiliations when needed later).

#### Scenario: Successful fetch
- **WHEN** fetch user receives a `DevicePlayer` with name `Ada` and team fields
- **THEN** the session retains that object
- **AND** welcome can display `Ada`

### Requirement: PIN remains separate from server UUID
The four-digit PIN from pin entry SHALL remain on `Player` via `getUserID` / `setUserID` for RNG seeding. The Alleycat player UUID in `DevicePlayer.id` SHALL be stored in session profile and SHALL NOT replace the PIN string used for seeding.

#### Scenario: PIN and UUID both present
- **WHEN** the player entered `0007` and the server returns a UUID in `DevicePlayer.id`
- **THEN** `getUserID()` remains `"0007"`
- **AND** the session profile includes the UUID

### Requirement: PlayerFetch avoids isHunter
PlayerFetch code SHALL NOT set or read `Player::isHunter`. Demo choose team SHALL NOT depend on hunter flag for persistence; stub only.

#### Scenario: Server path success
- **WHEN** fetch succeeds with team fields populated
- **THEN** registration code does not update `isHunter`

### Requirement: Gameplay Player migration is out of scope
This change SHALL NOT require global `Player` to fully replace legacy allegiance/faction/hunter fields for Idle or duel code. Unifying `Player` with `DevicePlayer` for gameplay is a follow-up before Quickdraw wiring.

#### Scenario: Standalone delivery
- **WHEN** PlayerFetch is built and tested in isolation
- **THEN** legacy gameplay code paths may still use the old `Player` fields unchanged
