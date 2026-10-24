## ADDED Requirements

### Requirement: Builder is pure
`buildQuickdrawMatchRecord` (or equivalent) SHALL map `DuelSession` completion data plus local and opponent `PlayerV2` profiles into one `QuickdrawMatchRecord` without I/O.

#### Scenario: Missing local profile
- **WHEN** local profile id is nil
- **THEN** build returns false

#### Scenario: Missing opponent id
- **WHEN** session opponentPlayerIdKnown is false
- **THEN** build returns false

### Requirement: Participant A is initiator
The output record SHALL set `playerAId` to the initiator UUID and `playerBId` to the responder UUID regardless of local link side.

#### Scenario: Local responder
- **WHEN** local was responder and times are complete
- **THEN** playerAId is the initiator profile id supplied to the builder
- **AND** playerBId is the responder profile id

### Requirement: Draw times follow A/B
`playerADrawTimeMs` and `playerBDrawTimeMs` SHALL equal initiator and responder draw times from the session.

#### Scenario: Times copied
- **WHEN** initiator time is 200 and responder time is 240
- **THEN** playerADrawTimeMs is 200 and playerBDrawTimeMs is 240

### Requirement: Winner placeholder
`winnerPlayerId` SHALL equal `playerAId` (initiator). The builder SHALL NOT compute a competitive winner from draw times.

#### Scenario: Winner field
- **WHEN** build succeeds
- **THEN** winnerPlayerId equals playerAId

### Requirement: Team snapshots
`playerATeamId` and `playerBTeamId` SHALL copy initiator and responder team ids from the respective profiles at build time. Nil team ids SHALL remain nil in the record struct for codec omission.

#### Scenario: Responder unassigned team
- **WHEN** responder profile has nil team id
- **THEN** playerBTeamId in the record is nil

### Requirement: Match id and timestamp
`matchId` SHALL come from the session shared duel UUID. `occurredAtUnixMs` SHALL come from the platform clock’s UTC milliseconds when available; zero is allowed when clock not synced (documented limitation).

#### Scenario: Clock available
- **WHEN** platform clock reports unix milliseconds 1700000000000
- **THEN** occurredAtUnixMs equals 1700000000000

### Requirement: No delta or sync flags
The builder SHALL NOT add fields beyond `QuickdrawMatchRecord` for delta_ms, sync_success, or failed_attempts.

#### Scenario: Record shape
- **WHEN** build succeeds
- **THEN** the struct contains only standard QuickdrawMatchRecord members
