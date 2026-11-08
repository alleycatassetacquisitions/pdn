## ADDED Requirements

### Requirement: PDN offers uncollected matches to its cabled FDN

While an FDN is Connected on one of its jacks, a PDN SHALL send that FDN its
oldest stored match record not yet collected, one record at a time, each
tagged with its storage sequence number. It SHALL resend the same record every
100 ms until the FDN acknowledges it. A record the ring has already overwritten
SHALL be skipped.

#### Scenario: Records go up in order
- **WHEN** a PDN holding uncollected records 4, 5, and 6 is cabled to an FDN
- **THEN** it sends record 4, and sends record 5 only after the FDN acknowledges 4

#### Scenario: Unplugging stops the transfer
- **WHEN** the FDN is unplugged while a record is unacknowledged
- **THEN** the PDN stops sending and resumes from that record the next time an FDN is cabled

### Requirement: Collection is acknowledged only after the FDN persists the record

An FDN SHALL acknowledge a record only after it has persisted it, or when it
already holds a record with the same match ID. The FDN SHALL hold up to 64
records awaiting upload and SHALL NOT acknowledge a new record while all 64
are held. A PDN SHALL mark records collected up to an acknowledged sequence
number, and SHALL ignore an acknowledgement for a sequence it has not stored
or has already marked collected.

#### Scenario: Lost acknowledgement does not duplicate the record
- **WHEN** an FDN persists a record but its acknowledgement is lost, and the PDN resends the record
- **THEN** the FDN acknowledges the resend without storing a second copy

#### Scenario: Full spool withholds the acknowledgement
- **WHEN** an FDN holding 64 records receives a 65th
- **THEN** it does not store or acknowledge it, and the PDN keeps the record uncollected

### Requirement: Collection frames come only from the cabled peer

A PDN SHALL accept an acknowledgement only from the MAC of an FDN Connected on
one of its jacks. An FDN SHALL accept a record only from the MAC of a PDN
Connected on one of its jacks. Either side SHALL drop a collection frame whose
length differs from its packet's fixed size.

#### Scenario: Acknowledgement from an uncabled device
- **WHEN** a PDN receives an acknowledgement from a MAC that is not its cabled FDN
- **THEN** the PDN's collected mark does not change

### Requirement: FDN uploads its records in one batch

When the FDN's idle upload check fires and it holds records, it SHALL enable
Wi-Fi and upload every held record in one `POST /quickdraw-matches` batch.
On any decodable response, including one listing per-match rejections, it SHALL
discard the uploaded records. On a transport error or an undecodable response
it SHALL keep them for the next upload. A held record that cannot be read SHALL
NOT be uploaded, SHALL NOT keep the records ahead of it from uploading, and SHALL
be discarded once it is the oldest held record.

#### Scenario: Server error keeps the batch
- **WHEN** the upload fails with a 5xx response
- **THEN** the FDN still holds every record it tried to upload

#### Scenario: Unreadable record does not block uploads
- **WHEN** the oldest held record cannot be read back from storage
- **THEN** the FDN discards it and uploads the remaining records
