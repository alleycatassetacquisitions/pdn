## ADDED Requirements

### Requirement: DuelSession stores exchange state without I/O
`DuelSession` SHALL hold in-memory duel data only. It SHALL NOT call storage, HTTP, or ESP-NOW. It SHALL NOT expose legacy `Match` or hunter/bounty fields.

#### Scenario: Clear session
- **WHEN** `clear()` is invoked on an active session
- **THEN** no match id, draw times, or completion flags remain set
- **AND** the session reports no active match

### Requirement: Link side is initiator or responder
Each session SHALL track `localLinkSide` as either initiator or responder for the lifetime of the match. Draw times SHALL be stored in **initiator** and **responder** slots, not hunter/bounty.

#### Scenario: Local initiator records draw
- **WHEN** `localLinkSide` is initiator and `recordLocalDrawTimeMs(200)` is called
- **THEN** the initiator draw time is 200 ms and marked present
- **AND** the responder draw time remains unset

#### Scenario: Remote draw from initiator on wire
- **WHEN** `localLinkSide` is responder and a remote draw arrives with wire initiator flag true and time 180 ms
- **THEN** the initiator draw time is 180 ms and marked present

### Requirement: Handshake fields
An active session SHALL store the shared match UUID string, opponent MAC (6 bytes), and whether opponent `PlayerV2.id` is known.

#### Scenario: Prime new session
- **WHEN** a match is primed with match id `M` and opponent MAC `mac`
- **THEN** the session is active with match id `M` and opponent MAC `mac`
- **AND** handshake-ready is false until the protocol layer sets it

### Requirement: Completion flags are explicit
The session SHALL track separately: local player pressed, remote draw result received, opponent never pressed, and grace expired without local press. These flags SHALL feed `DuelCompletionInputs` for the sync evaluator without embedding scoring rules in the session.

#### Scenario: Local press only
- **WHEN** local draw is recorded and remote result is not yet received
- **THEN** local pressed is true and remote result received is false

### Requirement: Opponent player id slot
The session SHALL store `opponentPlayerId` as `UUIDBytes` and a boolean `opponentPlayerIdKnown`. Finalize SHALL treat unknown opponent id as a hard failure at the coordinator/builder layer.

#### Scenario: Unknown opponent id
- **WHEN** opponent id is not known at finalize time
- **THEN** the record builder or coordinator refuses finalize without persisting
