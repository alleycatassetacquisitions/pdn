## ADDED Requirements

### Requirement: Record frame

A PDN SHALL send each crash record or match record in a record frame carrying, in order: the record kind (`1` byte: `0x01` crash record, `0x02` match record), a 4-byte record number, the record bytes (`42` for a crash record, `112` for a match record) and, for a match record only, its `16`-byte record tag (`record-authentication`). A record frame SHALL be sent as one peer-to-peer frame.

#### Scenario: Crash record frame
- **WHEN** a PDN sends crash record 7
- **THEN** the frame carries the crash record kind, record number `7` and the `42` crash record bytes, in that order and with nothing after them, in a single peer-to-peer frame

#### Scenario: Match record frame
- **WHEN** a PDN sends the match record with sequence number 12
- **THEN** the frame carries the match record kind, record number `12`, the `112` match record bytes and the `16`-byte record tag stored with that record, in that order, in a single peer-to-peer frame

### Requirement: Relay target without a cable

A PDN's relay target SHALL be the FDN that caused it to send: the FDN whose beacon it has just heard, or the FDN that has just become Connected on one of its jacks. A PDN SHALL act only on a beacon that identifies its sender as an FDN. Having a relay target SHALL NOT require a serial jack connection, and a PDN Connected on an FDN's jack SHALL still send its records.

#### Scenario: A PDN that is not plugged in gains a relay target from a beacon
- **WHEN** a PDN with nothing Connected on its jacks hears an FDN's beacon
- **THEN** that FDN is the relay target for the records the PDN sends in response

#### Scenario: A PDN plugged into one FDN can still deliver to another
- **WHEN** a PDN Connected on one FDN's jack hears a different FDN's beacon
- **THEN** the FDN that sent the beacon is the relay target for the records the PDN sends in response

#### Scenario: A beacon that does not identify an FDN is not a relay target
- **WHEN** a PDN with pending records hears a player-info beacon whose sender is not identified as an FDN
- **THEN** it sends no records to that sender

#### Scenario: No FDN in reach
- **WHEN** a PDN with pending records has no FDN Connected on a jack and hears no FDN beacon
- **THEN** it sends no records and the records stay pending

### Requirement: Relay triggers

A PDN SHALL send its lowest-numbered pending record of each record kind to the relay target when it hears an FDN beacon and when an FDN becomes Connected on one of its jacks. After a stored acknowledgement advances a mark, the PDN SHALL send the next pending record of that kind to the FDN that acknowledged. A PDN SHALL NOT send records while a match or a shootout is active on it; a stored acknowledgement arriving then SHALL still advance the mark.

#### Scenario: Each beacon is a new chance to deliver
- **WHEN** a PDN sends pending crash record 1 on hearing a beacon, no stored acknowledgement arrives, and it hears the next beacon `12,000 ms` later
- **THEN** the PDN sends crash record 1 again

#### Scenario: One record kind does not hold back the other
- **WHEN** a PDN has a pending crash record and a pending match record, and only the match record is acknowledged
- **THEN** the PDN sends its next pending match record while the crash record stays pending

#### Scenario: An acknowledgement during a match advances the mark and sends nothing
- **WHEN** the stored acknowledgement for a record a PDN sent just before a match arrives while that match is active
- **THEN** the PDN advances its mark and sends its next pending record only when it next hears a beacon or an FDN becomes Connected after the match

#### Scenario: No records are sent during a match
- **WHEN** a PDN with pending records hears an FDN's beacon while a match is active on it
- **THEN** the PDN sends nothing until the match has ended and it next hears a beacon or an FDN becomes Connected

### Requirement: One record of each kind in flight

A PDN SHALL send record frames by reliable unicast (`wireless-transport`) and SHALL have at most one record of each record kind in flight. A beacon or FDN connection that arrives while a record of a kind is still in flight SHALL NOT cause another send of that kind. When a match or a shootout becomes active the PDN SHALL cancel any record send in flight; the record stays pending.

#### Scenario: A match starting cancels a send in flight
- **WHEN** a match becomes active on a PDN while its transport is still retrying a record frame
- **THEN** the PDN stops retrying that frame and the record stays pending

#### Scenario: A burst of beacons does not multiply sends
- **WHEN** a PDN hears three FDN beacons within the time its transport is still retrying a crash record frame
- **THEN** it sends no further crash record frame until that send has been delivered or abandoned

### Requirement: Stored acknowledgement

An FDN's stored acknowledgement SHALL carry the record kind, the record number and a record check: the first `8` bytes of SHA-256 over the record bytes and, for a match record, the record tag that follows them, as the FDN stored them. A PDN SHALL treat a stored acknowledgement as answering a record only when its kind, number and record check all match the record frame as the PDN last sent it.

#### Scenario: An acknowledgement for the same match record bytes under a different tag does not count
- **WHEN** a PDN that sent match record 3 receives a stored acknowledgement for match record 3 whose record check was computed over the same record bytes and a different record tag
- **THEN** its record check does not match the frame the PDN sent, and the record stays pending

#### Scenario: An acknowledgement for different bytes under the same number does not count
- **WHEN** a PDN that sent crash record 3 receives a stored acknowledgement for crash record 3 whose record check does not match the record bytes it sent
- **THEN** the PDN does not treat its record as acknowledged and the record stays pending

### Requirement: Mark advances only on the stored acknowledgement

A PDN SHALL advance its mark for a record it has sent, and send the next, only when a stored acknowledgement answering its lowest pending record of that kind arrives from the FDN that record was most recently sent to. Any other stored acknowledgement SHALL be ignored. A mark SHALL NOT move backwards, and a radio delivery report SHALL NOT advance a mark. A record with no stored acknowledgement SHALL stay pending.

#### Scenario: Stored acknowledgement advances the mark
- **WHEN** a PDN sends pending match record 4 to an FDN and that FDN's stored acknowledgement for match record 4 arrives
- **THEN** the PDN advances its match mark to 4

#### Scenario: A delivered frame with no stored acknowledgement stays pending
- **WHEN** the radio reports a record frame delivered but no stored acknowledgement for that record arrives
- **THEN** the PDN's mark does not move and the record stays pending

#### Scenario: A late acknowledgement from an earlier FDN is ignored
- **WHEN** a PDN sends crash record 2 to one FDN, then on another FDN's beacon sends it to that FDN, and the first FDN's stored acknowledgement arrives afterwards
- **THEN** the PDN ignores it and advances its mark only on the second FDN's stored acknowledgement, sending crash record 3 once

#### Scenario: An acknowledgement from another device is ignored
- **WHEN** a stored acknowledgement answering the record a PDN just sent arrives from a device other than the FDN it was sent to
- **THEN** the PDN's mark does not move

### Requirement: FDN record acceptance without a cable

While its peer-to-peer radio is enabled an FDN SHALL accept a record frame from any PDN, Connected on a jack or not. It SHALL store the record kind, record number, record bytes, the record tag of a match record, and the sender's 6-byte MAC address (the frame's radio source address) unchanged in bounded non-volatile storage, and SHALL send the stored acknowledgement for that record only after storing.

#### Scenario: A record from a PDN that is not plugged in is stored and acknowledged
- **WHEN** an FDN with room for crash records receives a crash record frame from a PDN that is not Connected on any of its jacks
- **THEN** the FDN stores the record with the sender's MAC address and then sends the stored acknowledgement

#### Scenario: Acknowledgement follows storage
- **WHEN** an FDN loses power after receiving a record frame and before the record is in non-volatile storage
- **THEN** no stored acknowledgement was sent and the PDN still holds the record as pending

### Requirement: FDN drops malformed record frames

An FDN SHALL drop, without storing or acknowledging, a record frame whose record kind it does not know or whose length is not the length of a record frame of that kind.

#### Scenario: Unknown record kind
- **WHEN** an FDN receives a record frame whose record kind is neither crash record nor match record
- **THEN** it stores nothing and sends no stored acknowledgement

#### Scenario: Wrong length for the kind
- **WHEN** an FDN receives a frame marked as a crash record whose length is that of a match record frame
- **THEN** it stores nothing and sends no stored acknowledgement

### Requirement: FDN duplicates and full storage

An FDN SHALL treat a record frame as a duplicate only when it holds a record with the same sender, kind, number, record bytes and, for a match record, record tag, and SHALL acknowledge a duplicate without storing it again. A frame that matches a held record's sender, kind and number but differs in bytes or tag SHALL be stored as a separate record. When its storage for that record kind is full the FDN SHALL send no stored acknowledgement for a frame it would have to store.

#### Scenario: A resent record is acknowledged once more and stored once
- **WHEN** an FDN that holds a record receives the identical record frame from the same PDN again
- **THEN** it sends the stored acknowledgement and still holds one copy

#### Scenario: A different record under the same number does not displace the first
- **WHEN** an FDN holding crash record 5 from one MAC address receives a crash record frame with that MAC address and number 5 but different record bytes
- **THEN** it stores the new frame as a separate record and still holds the first

#### Scenario: A record that cannot be stored is not acknowledged
- **WHEN** an FDN whose storage for match records is full receives a match record frame
- **THEN** it sends no stored acknowledgement and the sending PDN keeps the record pending

### Requirement: FDN relays without verifying

An FDN SHALL NOT verify record tags and SHALL NOT hold a record key except in volatile memory while delivering one during registration (`player-provisioning`). When it uploads a relayed match record the FDN SHALL send the sender's MAC address, the record bytes and the record tag exactly as received. When it uploads a relayed crash record the FDN SHALL name the sender's MAC address as the originating device (`crash-reporting`).

#### Scenario: A record with a wrong tag is still relayed
- **WHEN** an FDN receives a well-formed match record frame whose record tag does not match its contents
- **THEN** the FDN stores, acknowledges and uploads it unchanged, leaving the decision to the server

#### Scenario: A match record with the all-zero tag is still relayed
- **WHEN** an FDN receives a match record frame whose record tag is `16` zero bytes
- **THEN** the FDN stores, acknowledges and uploads it with the all-zero record tag
