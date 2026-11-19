## ADDED Requirements

### Requirement: Serial jack application frame

Besides HELLO a serial jack SHALL carry one application frame: a kind byte, the sender's MAC, a length byte and at most `32` payload bytes, framed and CRC-checked like HELLO. A frame that fails its CRC or claims more than `32` bytes SHALL be dropped and its bytes rescanned for the next frame. The serial parser SHALL give the kind and payload no meaning. Every kind value SHALL be listed once, shared by both ends of a jack: `0` the record key, `1` the match secret.

#### Scenario: Frame arrives whole
- **WHEN** a device receives a CRC-valid application frame of kind `k` carrying `n ≤ 32` bytes
- **THEN** it hands up kind `k` and exactly those `n` bytes

#### Scenario: Corrupt frame is dropped and the next one read
- **WHEN** an application frame whose CRC fails is followed on the jack, with no gap, by a valid frame
- **THEN** the corrupt frame is not handed up and the valid one is

#### Scenario: Oversize frame is refused
- **WHEN** an application frame claims `33` or more payload bytes, or a sender asks to send that many
- **THEN** no frame is handed up or queued

### Requirement: Application frame sending

A device SHALL queue an application frame only for a peer Connected on that jack, at most one per jack, and SHALL write it straight after its next HELLO there. It SHALL report each accepted frame once, to the observer of its kind: written once every byte went to the jack, or dropped if the peer left before the write began. Every outcome SHALL be reported before another peer can be Connected on the jack.

#### Scenario: Frame waits for a Connected peer
- **WHEN** a device asks to send an application frame on a jack with no peer, or one still Connecting
- **THEN** the send is refused and nothing is written

#### Scenario: Frame follows the jack's HELLO
- **WHEN** a device sends an application frame on a jack whose peer is Connected
- **THEN** its next HELLO pass writes the frame straight after that jack's HELLO and on no other jack, and the sender later hears written

#### Scenario: One frame per jack at a time
- **WHEN** a device asks to send a second application frame on a jack before the first one's outcome is reported
- **THEN** the second send is refused

#### Scenario: Peer leaves before the write
- **WHEN** the peer on a jack goes silent while a frame for it is still waiting
- **THEN** the sender hears dropped and the frame is never written

### Requirement: Application frame routing by kind

A device SHALL hand a received application frame, with the jack it arrived on, only to the one receiver registered for its kind, and only if its source is the jack's HELLO peer. A frame from a peer still Connecting SHALL be held, the latest only, until the peer is Connected, and wiped if it leaves first. A frame of an unknown or unregistered kind SHALL be dropped. A second owner for a kind already held SHALL be refused.

#### Scenario: Frame reaches only its kind's receiver
- **WHEN** receivers are registered for kinds `A` and `B` and a frame of kind `B` arrives on the input jack
- **THEN** only the kind `B` receiver gets it, told it came on the input jack

#### Scenario: Unregistered kind is dropped
- **WHEN** a frame arrives whose kind has no receiver
- **THEN** no receiver gets it

#### Scenario: Frame before HELLO is dropped
- **WHEN** an application frame arrives on a jack that holds no HELLO peer
- **THEN** no receiver gets it

#### Scenario: Own echo and strangers are dropped
- **WHEN** a frame arrives whose source is this device or a MAC other than the jack's HELLO peer
- **THEN** no receiver gets it

#### Scenario: Frame from a Connecting peer waits
- **WHEN** a frame from the jack's HELLO peer arrives while that peer is still Connecting
- **THEN** the receiver gets it once the peer is Connected, or never if the peer leaves first

#### Scenario: Second receiver for a kind is refused
- **WHEN** a receiver is registered for a kind that already has one
- **THEN** the registration is refused and frames of that kind still reach the first receiver
