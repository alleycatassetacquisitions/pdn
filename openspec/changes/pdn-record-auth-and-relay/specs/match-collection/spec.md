## MODIFIED Requirements

### Requirement: PDN-to-FDN match transfer, FDN spool bounds, and watermark advancement

A PDN with pending match records SHALL send them to an FDN as `record-relay` requires:

- The PDN SHALL read retained uncollected match records in oldest-first order (from `max(collected_high_water_mark + 1, match_sequence_number - 31)` to `match_sequence_number`) and send them as record frames (`record-relay`) whose record number is the match record's sequence number on that PDN. A PDN with key ID `0` holds its match records (`record-authentication`).
- The PDN's persisted `collected_high_water_mark` SHALL be the mark that the stored acknowledgement advances (`record-relay`); the stored acknowledgement for a match record SHALL advance it to that match's sequence number.
- The FDN SHALL stage ingested PDN match records in a capacity-bounded non-volatile spool (retaining up to `64` match records awaiting server upload). Acceptance into the spool, the stored acknowledgement, duplicates and a full spool SHALL follow `record-relay`; while the spool is full the PDN retains the uncollected records in its own 32-slot ring buffer until the FDN drains its spool to the server.
- If the PDN loses contact with the FDN before all pending matches are acknowledged by the FDN, `collected_high_water_mark` SHALL reflect the last acknowledged match so only the remaining uncollected matches are sent on the next FDN contact.

#### Scenario: Partial FDN collection advances watermark only for acknowledged matches
- **WHEN** a PDN holding a record key has uncollected matches `1..5` and unplugs from a Connected FDN after the FDN acknowledges matches `1`, `2`, and `3`
- **THEN** the PDN's `collected_high_water_mark` advances to `3`, and on the next FDN contact the PDN transmits only matches `4` and `5`

#### Scenario: Spoofed wireless acknowledgement from uncabled sender is ignored
- **WHEN** a PDN with uncollected matches `1..5` sends match `1` to an FDN and receives an acknowledgement for match `1` from a device other than the FDN the record was sent to
- **THEN** the PDN ignores the acknowledgement and leaves `collected_high_water_mark` unchanged

#### Scenario: PDN not Connected on any jack delivers pending matches after a beacon
- **WHEN** a PDN holding a record key, with uncollected matches `1..3` and nothing Connected on either serial jack, hears an FDN's beacon, and that FDN's stored acknowledgements for matches `1`, `2`, and `3` arrive in turn
- **THEN** the PDN sends matches `1`, `2`, and `3` in ascending order as record frames with record numbers `1`, `2`, and `3`, and its `collected_high_water_mark` advances to `3`

### Requirement: FDN protobuf batch encoding and upload to server

An FDN holding ingested match records SHALL upload them to the central server in batches over HTTPS:

- Each upload batch SHALL contain between `1` and `100` match records; a request with `0` or more than `100` matches SHALL be rejected locally without queuing an HTTP request.
- The FDN SHALL encode the batch via the quickdraw-match protobuf codec as a `POST /quickdraw-matches` request with content type `application/protobuf`.
- For each match record in the batch, the codec SHALL write:
  - the match UUID, both player UUIDs (`player_a_id` and `player_b_id`), and the winner UUID (`winner_player_id`) formatted as lowercase `8-4-4-4-12` hexadecimal text;
  - each player's team UUID (`player_a_team_id`, `player_b_team_id`) as lowercase `8-4-4-4-12` hexadecimal text if and only if that team UUID is not nil;
  - both players' raw reaction times (`player_a_draw_time_ms`, `player_b_draw_time_ms`) as unsigned 64-bit millisecond values (`0..INT64_MAX`) and the UTC occurrence timestamp (`occurred_at_unix_ms`), marked present on the wire even when their numeric value is `0`.
- Each uploaded match SHALL also carry what `record-relay` requires of an uploaded relayed record, alongside the existing fields.

#### Scenario: Batch of matches is encoded and queued to POST /quickdraw-matches with raw reaction times and effective-time winner
- **WHEN** an FDN queues an upload of 2 collected match records (including a match won via posse boost where the winner's raw reaction time is `210 ms` against the opponent's `195 ms`, and one nil team UUID)
- **THEN** the FDN sends `POST /quickdraw-matches` with `application/protobuf`, both players' unsigned raw reaction times (`210 ms` and `195 ms`) and the posse-boosted winner's `winner_player_id` explicitly present on the wire, and the nil team UUID omitted

#### Scenario: Batch count outside 1..100 is rejected without queuing HTTP
- **WHEN** a caller attempts to encode a match upload batch with `0` or `101` records
- **THEN** the request is rejected locally and no HTTP request is queued

#### Scenario: Uploaded match carries its relayed record alongside the decoded fields
- **WHEN** an FDN uploads a match record it spooled from a PDN's record frame
- **THEN** the uploaded match carries, in addition to the decoded match fields, everything `record-relay` requires of an uploaded relayed record
