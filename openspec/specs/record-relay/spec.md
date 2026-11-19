# record-relay Specification

## Purpose
A PDN has no Wi-Fi, so it relays its crash and match records over ESP-NOW to any FDN it hears by beacon, one frame per record. The PDN retires a record only on that FDN's acknowledgement, which the FDN sends after storing it.

## Requirements

### Requirement: Record frame

A PDN SHALL send each crash record or match record as one peer-to-peer frame carrying, in order: the record kind (`0x01` crash, `0x02` match), a 4-byte record number and the record bytes (`42` for a crash record, `112` for a match record), and nothing after them.

#### Scenario: Crash record frame
- **WHEN** a PDN sends crash record 7
- **THEN** the frame carries the crash kind, record number `7` and the `42` crash record bytes, and nothing more

### Requirement: Relay target and triggers

A PDN SHALL send its lowest pending record of each kind to an FDN when it hears that FDN's beacon, and the next one after that FDN's stored acknowledgement. It SHALL act only on a beacon that identifies its sender as an FDN; no jack connection is needed. It SHALL NOT send while a match or shootout is active on it.

#### Scenario: A PDN that is not plugged in delivers on a beacon
- **WHEN** a PDN with nothing Connected on its jacks and a pending crash record hears an FDN's beacon
- **THEN** it sends that crash record to that FDN

#### Scenario: No records are sent during a match
- **WHEN** a PDN with pending records hears an FDN's beacon while a match is active on it
- **THEN** it sends nothing

### Requirement: One record of each kind in flight

A PDN SHALL send record frames by reliable unicast (`wireless-transport`) with at most one record of each kind in flight. A beacon heard while a record of that kind is in flight SHALL NOT cause another send of that kind. Crash and match records SHALL NOT hold each other back.

#### Scenario: A burst of beacons does not multiply sends
- **WHEN** a PDN hears three FDN beacons while its transport is still retrying a crash record frame
- **THEN** it sends no further crash record frame until that send is delivered or abandoned

### Requirement: Stored acknowledgement retires a record

An FDN's stored acknowledgement SHALL carry the record kind, the record number and a check over the record bytes. A PDN SHALL retire a record only on a stored acknowledgement whose kind, number and check match the frame it last sent, from the FDN it sent it to. A radio delivery report SHALL NOT retire a record. The check names the frame; it does not authenticate the sender.

#### Scenario: A delivered frame with no stored acknowledgement stays pending
- **WHEN** the radio reports a record frame delivered but no stored acknowledgement arrives
- **THEN** the record stays pending

### Requirement: FDN record intake

While its peer-to-peer radio is enabled an FDN SHALL accept a record frame from any PDN, Connected on a jack or not, store its kind, number, bytes and sender MAC in its non-volatile record store, and send the stored acknowledgement only after storing. It SHALL drop, without storing or acknowledging, a frame of unknown kind or wrong length.

#### Scenario: Acknowledgement follows storage
- **WHEN** an FDN loses power after receiving a record frame and before storing it
- **THEN** no stored acknowledgement was sent and the PDN still holds the record as pending

### Requirement: FDN duplicates and full storage

An FDN SHALL treat a frame as a duplicate only when it holds a record with the same sender, kind, number and bytes, or has uploaded and retired such a record and has not reused its slot or made it the next one written, and SHALL acknowledge a duplicate again without storing it twice. When its store is full for that kind it SHALL send no stored acknowledgement.

#### Scenario: A resent record is acknowledged again and stored once
- **WHEN** an FDN receives an identical record frame from the same PDN again
- **THEN** it sends the stored acknowledgement and still holds one copy

#### Scenario: A record resent after its upload is acknowledged and not stored again
- **WHEN** an FDN that uploaded and retired the last record it stored, then restarted, receives the identical frame from the same PDN
- **THEN** it sends the stored acknowledgement and neither stores nor uploads the record again
