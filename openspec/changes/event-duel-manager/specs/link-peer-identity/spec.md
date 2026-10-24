## ADDED Requirements

### Requirement: Finalize requires opponent UUID
The event duel stack SHALL NOT finalize and persist a record unless the opponent’s full `PlayerV2.id` is known to the local device.

#### Scenario: Bind before finalize
- **WHEN** coordinator bindOpponentPlayer (or equivalent) was called with opponent profile before finalize
- **THEN** build and append may succeed if sync succeeded

#### Scenario: Never bound
- **WHEN** finalize is requested and opponentPlayerIdKnown is false
- **THEN** finalize returns false
- **AND** queue count is unchanged

### Requirement: Legacy wire id is insufficient
The stack SHALL NOT treat the 4-character `QuickdrawCommand.playerId` field as a complete `PlayerV2.id` for upload records.

#### Scenario: Handshake only sends legacy id
- **WHEN** MATCH_ID_ACK carries a 4-character player id
- **THEN** opponent UUID is still unknown until bind or a future wire extension

### Requirement: Coordinator exposes explicit bind
`MatchManagerV2` SHALL expose an explicit API for gameplay/connect code to supply opponent `PlayerV2` (or at minimum opponent id + team snapshot) before finalize.

#### Scenario: Connect binds peer
- **WHEN** Node connect logic knows peer profile from product rules
- **THEN** it calls bind before duel ends

### Requirement: Follow-up wire extension tracked
This capability documents a **follow-up change** to exchange full UUIDs during handshake without relying on bind. That change is out of scope for the first implementation PR of this stack.

#### Scenario: Future packet
- **WHEN** a future wire version carries opponent UUID in handshake
- **THEN** bind may become optional if session sets opponentPlayerIdKnown from protocol alone
