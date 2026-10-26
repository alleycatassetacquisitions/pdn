## ADDED Requirements

### Requirement: DuelSession holds duel facts without I/O
`DuelSession` SHALL store in-memory duel state only. It SHALL NOT access storage, HTTP, or ESP-NOW, and SHALL NOT expose hunter/bounty fields or a legacy `Match`.

#### Scenario: Clear
- **WHEN** `clear()` is called on an active session
- **THEN** the session reports no active match
- **AND** match id, peer identity, draw times, and completion flags are reset

### Requirement: Link side is fixed at prime time
A session SHALL record `DuelLinkSide` (`INITIATOR` or `RESPONDER`) when primed and SHALL NOT change it until cleared.

#### Scenario: Side is stable
- **WHEN** a session is primed as responder
- **THEN** the local side remains responder until `clear()`

### Requirement: Participants captured at handshake
A primed session SHALL hold the shared match id (`UUIDBytes`), opponent MAC, and both participants' player ids and team ids. A nil team id means unassigned.

#### Scenario: Prime
- **WHEN** a session is primed with match id `M`, MAC `mac`, and both participants' ids and teams
- **THEN** each value is readable from the session
- **AND** handshake-complete is false until the manager marks it complete

### Requirement: Draw times use initiator and responder slots
`recordLocalDrawTimeMs` SHALL write the slot for the local link side. `recordRemoteDrawTimeMs` SHALL write the slot for the opposite side. Each slot has a present flag.

#### Scenario: Local initiator
- **WHEN** local side is initiator and `recordLocalDrawTimeMs(200)` is called
- **THEN** the initiator time is 200 and present
- **AND** the responder time is not present

#### Scenario: Remote from initiator
- **WHEN** local side is responder and a remote draw of 180 is recorded
- **THEN** the initiator time is 180 and present

### Requirement: Duplicate draws are ignored
Once a slot is present, further writes to that slot SHALL be ignored and SHALL leave the stored value unchanged.

#### Scenario: Second draw
- **WHEN** the remote draw is recorded twice with different values
- **THEN** the first value is kept

### Requirement: Completion flags are explicit
The session SHALL track separately: local pressed, remote result received, opponent never pressed, and grace expired without local press. `isExchangeComplete()` SHALL be true when both draw times are present, or when opponent-never-pressed or grace-without-press applies. It SHALL NOT imply sync success.

#### Scenario: Both pressed
- **WHEN** both draw times are present
- **THEN** `isExchangeComplete()` is true

#### Scenario: In progress
- **WHEN** only the local time is present and neither exception flag is set
- **THEN** `isExchangeComplete()` is false

#### Scenario: Never pressed
- **WHEN** opponent-never-pressed is set
- **THEN** `isExchangeComplete()` is true

### Requirement: Agreed timestamp
The session SHALL hold `occurredAtUnixMs`, set at prime time from the initiator's offer.

#### Scenario: Responder adopts timestamp
- **WHEN** a responder session is primed from an offer carrying 1700000000000
- **THEN** the session timestamp is 1700000000000

### Requirement: Projection to upload record
`tryBuildUploadRecord(QuickdrawMatchRecord& out)` SHALL fill `out` and return true only when the handshake is complete, both draw times are present, and both participant ids are non-nil. Otherwise it SHALL return false and leave `out` unchanged.

The record SHALL set: `matchId` from the session; `playerAId`/`playerBId` from the initiator/responder ids; `playerATeamId`/`playerBTeamId` from the initiator/responder team ids; `playerADrawTimeMs`/`playerBDrawTimeMs` from the initiator/responder times; `winnerPlayerId` equal to `playerAId`; `occurredAtUnixMs` from the session timestamp. It SHALL NOT compute a faster-draw winner.

#### Scenario: Local responder
- **WHEN** the local device is responder and the session is complete
- **THEN** `playerAId` is the initiator's id and `playerBId` is the local id

#### Scenario: Winner
- **WHEN** build succeeds
- **THEN** `winnerPlayerId` equals `playerAId`

#### Scenario: Unassigned team
- **WHEN** the responder's team id is nil
- **THEN** `playerBTeamId` in the record is nil

#### Scenario: Incomplete
- **WHEN** the responder time is not present
- **THEN** `tryBuildUploadRecord` returns false
