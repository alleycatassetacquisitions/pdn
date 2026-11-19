## MODIFIED Requirements

### Requirement: Record frame

A PDN SHALL send each crash record or match record as one peer-to-peer frame carrying, in order: the record kind (`0x01` crash, `0x02` match), a 4-byte record number, the record bytes (`42` for a crash record, `112` for a match record), then for a match record its `16`-byte record tag as stored (`record-authentication`), and nothing after them.

#### Scenario: Crash record frame
- **WHEN** a PDN sends crash record 7
- **THEN** the frame carries the crash kind, record number `7` and the `42` crash record bytes, and nothing more

#### Scenario: Match record frame carries its tag
- **WHEN** a PDN sends match record 3
- **THEN** the frame carries the match kind, record number `3`, the `112` record bytes and the `16`-byte tag stored with them, and nothing more

### Requirement: Stored acknowledgement retires a record

An FDN's stored acknowledgement SHALL carry the record kind, the record number and a check over the record bytes and, for a match record, its tag. A PDN SHALL retire a record only on a stored acknowledgement whose kind, number and check match the frame it last sent, from the FDN it sent it to. A radio delivery report SHALL NOT retire a record. The check names the frame; it does not authenticate the sender.

#### Scenario: A delivered frame with no stored acknowledgement stays pending
- **WHEN** the radio reports a record frame delivered but no stored acknowledgement arrives
- **THEN** the record stays pending

#### Scenario: An acknowledgement for the same bytes under another tag does not retire
- **WHEN** a PDN that sent match record 3 receives a stored acknowledgement for match record 3 whose check covers the same record bytes and a different tag
- **THEN** the record stays pending

### Requirement: FDN record intake

While its peer-to-peer radio is enabled an FDN SHALL accept a record frame from any PDN, Connected on a jack or not, store its kind, number, bytes, the tag of a match record and the sender MAC in its non-volatile record store, and send the stored acknowledgement only after storing. It SHALL drop, without storing or acknowledging, a frame of unknown kind or wrong length.

#### Scenario: Acknowledgement follows storage
- **WHEN** an FDN loses power after receiving a record frame and before storing it
- **THEN** no stored acknowledgement was sent and the PDN still holds the record as pending

### Requirement: FDN duplicates and full storage

An FDN SHALL treat a frame as a duplicate only when it holds a record with the same sender, kind, number, bytes and, for a match record, tag, and SHALL acknowledge a duplicate again without storing it twice. A frame that differs from a held record only in its tag SHALL be stored as a separate record. When its store is full for that kind it SHALL send no stored acknowledgement.

#### Scenario: A resent record is acknowledged again and stored once
- **WHEN** an FDN receives an identical record frame from the same PDN again
- **THEN** it sends the stored acknowledgement and still holds one copy

#### Scenario: The same bytes under another tag are stored separately
- **WHEN** an FDN holding match record 9 from a PDN receives match record 9 from that PDN with the same bytes and a different tag
- **THEN** it stores the new frame as a separate record, acknowledges it, and still holds the first

## ADDED Requirements

### Requirement: FDN relays without verifying

An FDN SHALL NOT verify record tags and SHALL NOT hold a record key except while handing one to a PDN during registration (`player-provisioning`). It SHALL store and acknowledge a match record with its tag exactly as received, leaving every decision to the server. Uploading the tag waits on the server's upload format (#486 Q2); until then the upload carries the record bytes only.

#### Scenario: A record with a wrong tag is still stored
- **WHEN** an FDN receives a well-formed match record frame whose tag does not match its bytes
- **THEN** the FDN stores and acknowledges it unchanged
