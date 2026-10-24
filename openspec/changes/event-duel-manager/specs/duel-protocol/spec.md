## ADDED Requirements

### Requirement: Protocol adapts QuickdrawCommand to DuelSession
`DuelProtocol` SHALL translate inbound `QuickdrawCommand` values into `DuelSession` mutations and SHALL emit outbound commands through `QuickdrawWirelessManager`. It SHALL NOT append storage or build upload records.

#### Scenario: Unexpected command
- **WHEN** an unsupported command byte is received
- **THEN** the session is unchanged
- **AND** no outbound packet is sent

### Requirement: Wire initiator flag
On the event path, `QuickdrawCommand::isHunter` SHALL mean **remote sender is initiator** when true and **responder** when false. Documentation and logs SHALL refer to link side, not hunter/bounty.

#### Scenario: Draw result from initiator
- **WHEN** a DRAW_RESULT arrives with isHunter true and time 220 ms
- **THEN** the session initiator draw time becomes 220 ms

### Requirement: SEND_MATCH_ID direct-peer gate
Inbound `SEND_MATCH_ID` SHALL be accepted only when `RemoteDeviceCoordinator::isDirectPeer(senderMac)` is true. Otherwise the command SHALL be ignored with no session mutation.

#### Scenario: Stranger MAC
- **WHEN** SEND_MATCH_ID arrives from a MAC that is not a direct peer
- **THEN** no session is created

### Requirement: Post-handshake opponent gate
`MATCH_ID_ACK`, `MATCH_ROLE_MISMATCH`, `DRAW_RESULT`, and `NEVER_PRESSED` SHALL be handled only when the sender MAC and match id match the active session opponent and match id.

#### Scenario: Wrong match id
- **WHEN** DRAW_RESULT arrives with a different match id than the active session
- **THEN** draw times are unchanged

### Requirement: Initiator starts handshake
When local link side is initiator, the protocol SHALL send `SEND_MATCH_ID` with isHunter true (initiator). When local is responder, inbound SEND_MATCH_ID with initiator true SHALL prime the session and reply with `MATCH_ID_ACK` with isHunter false.

#### Scenario: Responder accepts initiator
- **WHEN** responder receives valid SEND_MATCH_ID from direct peer with initiator true
- **THEN** session is primed with the packet match id
- **AND** MATCH_ID_ACK is sent with initiator false

### Requirement: Link role mismatch
When inbound SEND_MATCH_ID claims initiator and local link side is also initiator, the protocol SHALL send `MATCH_ROLE_MISMATCH` and SHALL NOT prime a session.

#### Scenario: Dual initiator
- **WHEN** both sides believe they are initiator on the wire
- **THEN** no handshake-ready session results on the rejecting device

### Requirement: Outbound draw result uses local link side
When sending DRAW_RESULT or NEVER_PRESSED, the protocol SHALL set isHunter to true iff local link side is initiator.

#### Scenario: Responder sends draw
- **WHEN** local is responder and sends draw time 300 ms
- **THEN** outbound DRAW_RESULT has isHunter false and time 300 ms

### Requirement: No shootout or chain branches
`DuelProtocol` SHALL NOT reference shootout managers, chain duel managers, or boost providers.

#### Scenario: Compile boundary
- **WHEN** event firmware links DuelProtocol
- **THEN** shootout and chain types are not required
