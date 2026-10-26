## ADDED Requirements

### Requirement: Dedicated packet and packet type
The NodeNetwork duel path SHALL use `NodeNetworkPacket` carried under its own `PktType` value. It SHALL NOT use `QuickdrawPacket`, `QuickdrawCommand`, or `QDCommand`.

#### Scenario: Compile boundary
- **WHEN** NodeNetwork duel code is built
- **THEN** it does not include `quickdraw-wireless-manager.hpp`

### Requirement: Packet carries full identities
`NodeNetworkPacket` SHALL carry: protocol version, command, sender link side, match id (16 bytes), sender player id (16 bytes), sender team id (16 bytes, nil when unassigned), draw time in milliseconds, and an initiator timestamp in unix milliseconds. Commands SHALL be `OFFER`, `ACCEPT`, `ROLE_MISMATCH`, `TIME_CORRECTION`, `DRAW`, and `NEVER_PRESSED`.

#### Scenario: Round trip
- **WHEN** a packet is encoded and decoded
- **THEN** every field equals the original

### Requirement: Malformed packets are dropped
A receive with the wrong length or an unsupported protocol version SHALL be logged, return failure, and cause no manager call.

#### Scenario: Wrong length
- **WHEN** data shorter than the packet size is received
- **THEN** the manager is not called

#### Scenario: Wrong version
- **WHEN** the version byte does not match
- **THEN** the manager is not called

### Requirement: Offer is gated on a direct peer
An inbound `OFFER` SHALL be passed to the manager only when `RemoteDeviceCoordinator::isDirectPeer(senderMac)` is true.

#### Scenario: Stranger
- **WHEN** an offer arrives from a MAC that is not a direct peer
- **THEN** the manager is not called and no reply is sent

### Requirement: Later commands are gated on the active opponent
`ACCEPT`, `ROLE_MISMATCH`, `TIME_CORRECTION`, `DRAW`, and `NEVER_PRESSED` SHALL be passed through only when the sender MAC equals the active session's opponent MAC and the match id equals the active session's match id. For `TIME_CORRECTION`, the "active session" is the initiator's pending offer.

#### Scenario: Wrong match id
- **WHEN** a draw arrives with a match id different from the active session
- **THEN** no session state changes

### Requirement: Handshake
An initiator SHALL send `OFFER` containing match id, its player id and team id, and its current unix time. A responder receiving a valid offer SHALL prime its session from the offer, adopt the offered timestamp, and reply `ACCEPT` with its own player id and team id. The initiator receiving `ACCEPT` SHALL record the responder's ids and mark the handshake complete. The responder SHALL mark the handshake complete after sending `ACCEPT`.

#### Scenario: Responder accepts
- **WHEN** a responder receives a valid offer from a direct peer
- **THEN** its session holds the initiator's ids and the offered timestamp
- **AND** an `ACCEPT` carrying the responder's ids is sent

### Requirement: Timestamp validation
An initiator SHALL NOT send an offer when its unix time is not positive. A responder SHALL reject an offer whose timestamp is not positive and SHALL NOT prime a session from it.

#### Scenario: Unsynced clock
- **WHEN** the initiator's clock reports 0
- **THEN** no offer is sent and no session is primed

### Requirement: Time correction
A responder SHALL treat an offer timestamp as implausible when `offerTimestamp * TIME_IMPLAUSIBLE_RATIO < responderTimestamp` (`TIME_IMPLAUSIBLE_RATIO` = 1000), which indicates the initiator never obtained a valid unix time. In that case the responder SHALL NOT prime a session and SHALL reply `TIME_CORRECTION` carrying its own unix time in the timestamp field. The responder SHALL NOT send a correction when its own timestamp is not positive. An offer timestamp greater than or equal to the responder's, or less by a smaller factor, SHALL be accepted.

An initiator receiving `TIME_CORRECTION` for its pending offer SHALL re-send the offer once, with the same match id and the corrected timestamp. It SHALL NOT re-send more than once per match id. The correction SHALL NOT change the device's platform clock.

#### Scenario: Initiator without valid time
- **WHEN** the initiator offers timestamp 90000 and the responder's time is 1700000000000
- **THEN** the responder sends `TIME_CORRECTION` with 1700000000000 and no session is primed
- **AND** the initiator re-sends the offer with 1700000000000
- **AND** the responder accepts the second offer

#### Scenario: Ordinary skew
- **WHEN** the initiator's timestamp differs from the responder's by a few seconds
- **THEN** the offer is accepted and no correction is sent

#### Scenario: No correction loop
- **WHEN** the re-sent offer is still implausible
- **THEN** the initiator does not re-send again

#### Scenario: Responder clock invalid
- **WHEN** the responder's own timestamp is not positive and the offer is implausible
- **THEN** no session is primed and no correction is sent

### Requirement: Role mismatch
An inbound `OFFER` received while the local side is initiator SHALL cause a `ROLE_MISMATCH` reply and no session priming.

#### Scenario: Two initiators
- **WHEN** a device configured as initiator receives an offer
- **THEN** it replies `ROLE_MISMATCH`
- **AND** its session is unchanged

### Requirement: Draw and never-pressed messages
Local draw reports SHALL send `DRAW` with the local time and local link side. A local grace expiry without a press SHALL send `NEVER_PRESSED`. The receiver SHALL record the sender's draw time into the sender's slot, and for `NEVER_PRESSED` SHALL also set opponent-never-pressed.

#### Scenario: Responder draw
- **WHEN** the local responder reports 300 ms
- **THEN** a `DRAW` is sent with time 300 and responder link side

### Requirement: Duplicates ignored
A repeated `DRAW` or `NEVER_PRESSED` from a side whose slot is already present SHALL be ignored.

#### Scenario: Repeat
- **WHEN** the same `DRAW` is received twice
- **THEN** the session value is unchanged and no error is raised

### Requirement: No tournament coupling
The wireless manager SHALL NOT reference shootout, chain duel, boost, or legacy `Player` stats.

#### Scenario: Linkage
- **WHEN** NodeNetwork links the event wireless manager
- **THEN** `ShootoutManager` and `ChainDuelManager` are not required
