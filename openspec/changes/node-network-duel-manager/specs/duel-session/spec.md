## ADDED Requirements

### Requirement: DuelSession holds duel facts without I/O
`DuelSession` SHALL store in-memory duel state only. It SHALL NOT access storage, HTTP, or ESP-NOW, and SHALL NOT expose hunter/bounty fields or a legacy `Match`.

#### Scenario: Clear
- **WHEN** `clear()` is called on an active session
- **THEN** the session reports no active match
- **AND** match id, peer identity, sequence, step results, and outcome are reset

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

### Requirement: Clock sync samples
`recordSyncSample(offsetMs, roundTripMs)` SHALL keep only the sample with the smallest round trip. The offset is responder uptime minus initiator uptime.

#### Scenario: Best sample kept
- **WHEN** samples with round trips 40, 12, and 30 are recorded
- **THEN** the session holds the offset and round trip of the 12 ms sample

### Requirement: Input sequence and schedule
The session SHALL hold the `NodeInputSequence` and step 0's start on the local uptime on both devices. Each of the five `NodeInput` steps SHALL hold one `NodePrompt` per side (`initiator`, `responder`), each a count of 0–2 buttons and the buttons in press order; a side presses a step when its count is above 0, and at least one side presses every step. `setSequence(sequence, stepZeroAtMs)` SHALL keep the first sequence and reject later calls. The session SHALL report each side's prompt for a step (an empty prompt out of range) and whether the local device and the opponent press it.

#### Scenario: Both players press
- **WHEN** step 0 gives the initiator one button and the responder two
- **THEN** both `isLocalPresser(0)` and `isOpponentPresser(0)` are true on either device

#### Scenario: First sequence kept
- **WHEN** a second sequence is set
- **THEN** the call returns false and the first sequence and step 0 time remain

### Requirement: Delivery and liveness flags
The session SHALL record, for `SEQUENCE` and for `OUTCOME`, whether it was acknowledged or ran out of resends. `clear()` SHALL reset them.

#### Scenario: Cleared
- **WHEN** a session with the sequence delivered is cleared
- **THEN** every delivered and undelivered flag is false

### Requirement: Step results
Each pressing side SHALL have a part result of `PENDING`, `SUCCESS`, or `FAILED` per step. `recordStepResult(step, side, success)` SHALL accept a part only once a sequence is set, for a step in range that the side presses and whose part is still `PENDING`. A step's result SHALL be `FAILED` if any part failed, `PENDING` if any part is still pending, and `SUCCESS` otherwise, so a both-player step succeeds only when both parts succeed and fails as soon as either part fails. `countSuccesses(side)` SHALL credit each successful step to the initiator when the initiator presses it (including both-player steps) and to the responder otherwise, so the two counts sum to the steps passed. The session SHALL also keep, per step, the highest press count the opponent has reported, for drawing the opponent's prompt.

#### Scenario: First result kept
- **WHEN** step 0 is recorded as failed and then as success
- **THEN** step 0 stays failed

#### Scenario: Both-player step
- **WHEN** both sides press step 0 and only the responder's part has succeeded
- **THEN** step 0 is pending; once the initiator's part succeeds it is a success credited to the initiator

#### Scenario: No sequence
- **WHEN** a result is recorded before a sequence is set
- **THEN** it is rejected

### Requirement: Outcome
The session SHALL hold the outcome as an initiator score and a responder score. `getBestKnownScore()` SHALL return the outcome total when known, otherwise the local success count. `isRoundSuccess()` SHALL be true only when the outcome is known and the total is at least `NODE_SUCCESS_THRESHOLD` (4).

#### Scenario: Without outcome
- **WHEN** no outcome is known
- **THEN** `isRoundSuccess()` is false

### Requirement: Agreed timestamp
The session SHALL hold `occurredAtUnixMs`, set at prime time from the initiator's offer.

#### Scenario: Responder adopts timestamp
- **WHEN** a responder session is primed from an offer carrying 1700000000000
- **THEN** the session timestamp is 1700000000000

### Requirement: Projection to upload record
`tryBuildUploadRecord(QuickdrawMatchRecord& out)` SHALL fill `out` and return true only when the handshake is complete, the outcome is known, and both participant ids are non-nil. Otherwise it SHALL return false and leave `out` unchanged.

The record SHALL set: `matchId` from the session; `playerAId`/`playerBId` from the initiator/responder ids; `playerATeamId`/`playerBTeamId` from the initiator/responder team ids; `playerADrawTimeMs`/`playerBDrawTimeMs` from the initiator/responder success counts; `winnerPlayerId` equal to `playerAId`; `occurredAtUnixMs` from the session timestamp. It SHALL NOT compute a faster-draw winner.

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
- **WHEN** the outcome is not known
- **THEN** `tryBuildUploadRecord` returns false
