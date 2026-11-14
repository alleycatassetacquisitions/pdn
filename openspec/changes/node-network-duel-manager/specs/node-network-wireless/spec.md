## ADDED Requirements

### Requirement: Dedicated packet and packet type
The NodeNetwork duel path SHALL use `NodeNetworkPacket` carried under its own `PktType` value. It SHALL NOT use `QuickdrawPacket`, `QuickdrawCommand`, or `QDCommand`.

#### Scenario: Compile boundary
- **WHEN** NodeNetwork duel code is built
- **THEN** it does not include `quickdraw-wireless-manager.hpp`

### Requirement: Packet layout
`NodeNetworkPacket` SHALL be a packed struct of `NODE_NETWORK_PACKET_WIRE_SIZE` (87) bytes carrying: command, sender link side, reliable flag, a 16-bit sequence number, match id, sender player id, and sender team id (16 bytes each, nil team when unassigned), step index, input success, press count (one byte each), five step-prompt bytes, initiator score, responder score (one byte each), SYNC origin/receive/transmit uptimes and step-0 uptime (`uint32` each), and a unix-millisecond timestamp (`int64`). Each step-prompt byte SHALL hold the initiator's prompt in the low nibble and the responder's in the high nibble; within a nibble, bits 0–1 are the button count (0–2), bit 2 marks the first button as secondary, and bit 3 marks the second. Field offsets SHALL be checked at compile time. There is no protocol version and no compatibility with other layouts.

Commands SHALL be `OFFER`, `ACCEPT`, `ROLE_MISMATCH`, `TIME_CORRECTION`, `SYNC`, `SYNC_REPLY`, `SEQUENCE`, `INPUT_RESULT`, `OUTCOME`, `ABORT`, `ACK`, and `INPUT_PRESS`.

#### Scenario: Round trip
- **WHEN** a packet is encoded and decoded
- **THEN** every field equals the original

### Requirement: Malformed packets are dropped
A receive with the wrong length or a command outside `OFFER`..`ACK` SHALL be logged, return failure, and reach no callback.

#### Scenario: Wrong length
- **WHEN** data shorter than the packet size is received
- **THEN** the packet callback is not called

#### Scenario: Unknown command
- **WHEN** the command byte is outside the command set
- **THEN** the packet callback is not called

### Requirement: Generic reliable delivery
`sendReliable(mac, packet)` SHALL set the reliable flag, assign the sender's next sequence number, send once, and keep the packet pending. `update()` SHALL resend a pending packet every `NODE_RELIABLE_RETRY_MS` (100) until it is acknowledged or `NODE_RELIABLE_MAX_ATTEMPTS` (8) sends have gone out. Up to `NODE_RELIABLE_PENDING_CAPACITY` (16) packets MAY be pending; `sendReliable` SHALL return failure when the table is full. Resends SHALL continue after the session that sent them is cleared.

The delivery callback SHALL fire once per reliable packet: with `delivered` true when a matching `ACK` arrives, or false when attempts run out.

#### Scenario: Acknowledged
- **WHEN** an `ACK` echoing the sequence number and match id arrives from the destination
- **THEN** the packet is no longer pending and the delivery callback reports delivered

#### Scenario: Never acknowledged
- **WHEN** no `ACK` arrives within eight sends
- **THEN** the delivery callback reports undelivered and no further sends happen

#### Scenario: ACK from someone else
- **WHEN** an `ACK` with the right sequence number arrives from a different MAC
- **THEN** the packet stays pending

### Requirement: Receiving reliable packets
A received packet with the reliable flag SHALL be answered with an `ACK` echoing its sequence number and match id, every time it is received. A repeat, identified by sender MAC, match id, and sequence number among the last `NODE_RELIABLE_SEEN_CAPACITY` (16) reliable packets, SHALL be acknowledged but not passed to the packet callback. `ACK`s SHALL be consumed by the wireless layer and not passed on.

`processPacket` and `update` SHALL run on the main loop only. The ESP-NOW driver queues frames on the WiFi task and dispatches them from `exec()`, so the wireless manager and session need no locking.

#### Scenario: Lost ACK
- **WHEN** the same reliable packet is received twice
- **THEN** two `ACK`s are sent and the packet callback runs once

### Requirement: Gating is the manager's job
The wireless layer SHALL NOT gate by peer or session. `MatchManagerV2` SHALL pass an `OFFER` on only when `RemoteDeviceCoordinator::isDirectPeer(senderMac)` is true, and every later command only when the sender MAC and match id equal the active session's and the sender's link side is the opponent's.

#### Scenario: Stranger
- **WHEN** an offer arrives from a MAC that is not a direct peer
- **THEN** no session is primed and no reply is sent

#### Scenario: Wrong match id
- **WHEN** a `SEQUENCE` arrives with a match id different from the active session
- **THEN** no session state changes

### Requirement: Handshake
An initiator SHALL send `OFFER` containing match id, its player id and team id, and its current unix time. A responder receiving a valid offer SHALL prime its session from the offer, adopt the offered timestamp, and reply `ACCEPT` with its own player id and team id. The initiator receiving `ACCEPT` SHALL record the responder's ids and mark the handshake complete. The responder SHALL mark the handshake complete after sending `ACCEPT`. `OFFER`, `ACCEPT`, `ROLE_MISMATCH`, and `TIME_CORRECTION` are unreliable; the idle state re-offers on timeout.

#### Scenario: Responder accepts
- **WHEN** a responder receives a valid offer from a direct peer
- **THEN** its session holds the initiator's ids and the offered timestamp
- **AND** an `ACCEPT` carrying the responder's ids is sent

### Requirement: Timestamp validation
An initiator SHALL NOT send an offer when its unix time is not positive. A responder SHALL reject an offer whose timestamp is not positive and SHALL NOT prime a session from it. The unix timestamp is used only for the upload record, never for round timing.

#### Scenario: Unsynced clock
- **WHEN** the initiator's clock reports 0
- **THEN** no offer is sent and no session is primed

### Requirement: Time correction
A responder SHALL treat an offer timestamp as implausible when it is smaller than the responder's timestamp divided by `TIME_IMPLAUSIBLE_RATIO` (1000), which indicates the initiator never obtained a valid unix time. In that case the responder SHALL NOT prime a session and SHALL reply `TIME_CORRECTION` carrying its own unix time. The responder SHALL NOT send a correction when its own timestamp is not positive.

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

### Requirement: Round messages
Only the initiator SHALL send `SYNC`, `SEQUENCE`, and `OUTCOME`; only the responder SHALL send `SYNC_REPLY`. Either side SHALL send `INPUT_RESULT` (with its press count) for its part of the steps it presses, SHALL send `INPUT_PRESS` after each correct press that does not finish its prompt, and MAY send `ABORT`. `SEQUENCE`, `INPUT_RESULT`, `OUTCOME`, and `ABORT` SHALL be sent reliably; `SYNC`, `SYNC_REPLY`, and `INPUT_PRESS` SHALL NOT (a resent probe would corrupt the round-trip measurement, and a lost press only delays the viewer's inversion until `INPUT_RESULT`). A round packet arriving at the wrong side, an `INPUT_RESULT` or `INPUT_PRESS` for a step the opponent does not press, and an `OUTCOME` whose scores sum to more than 5 SHALL be ignored.

#### Scenario: Responder result
- **WHEN** the local responder judges step 3 as a success after one press
- **THEN** a reliable `INPUT_RESULT` is sent with step 3, success 1, press count 1, and responder link side

#### Scenario: Partial press
- **WHEN** the local player correctly presses the first button of a two-button prompt
- **THEN** an unreliable `INPUT_PRESS` is sent with the step and press count 1

### Requirement: Duplicates ignored
A repeated `INPUT_RESULT` for a resolved step, a second `SEQUENCE`, or a second `OUTCOME` SHALL leave the session unchanged.

#### Scenario: Repeat
- **WHEN** the same `INPUT_RESULT` is received twice
- **THEN** the session value is unchanged and no error is raised

### Requirement: No tournament coupling
The wireless manager SHALL NOT reference shootout, chain duel, boost, or legacy `Player` stats.

#### Scenario: Linkage
- **WHEN** NodeNetwork links the event wireless manager
- **THEN** `ShootoutManager` and `ChainDuelManager` are not required
