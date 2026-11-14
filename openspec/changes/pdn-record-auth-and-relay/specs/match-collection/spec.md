## MODIFIED Requirements

### Requirement: Bounded persistent match ring buffer and collection watermark on PDN

Every completed standalone 1v1 match on a PDN that holds a record key when the match ends SHALL be persisted in a bounded non-volatile ring buffer in binary form alongside a monotonic sequence counter and collection watermark:

- Each persisted standalone match SHALL increment the PDN's persisted `match_sequence_number` by `1` (starting at `1`) and write the binary match record into a `32`-slot circular buffer indexed by `(match_sequence_number - 1) mod 32`.
- The PDN SHALL write each match record's `16`-byte record tag (`record-authentication`) into the same slot as the record. A match that ends while the PDN holds no record key SHALL NOT be persisted (`record-authentication`).
- A stored slot that does not hold `128` bytes, such as a slot of `112` bytes and no record tag written by firmware that stored none, cannot be sent. Such a slot SHALL NOT be pending, whatever its sequence number: the PDN SHALL NOT send it, and SHALL send the pending records after it as if it were absent.
- Binary match records containing raw 16-byte UUIDs (which may include `0x00` bytes) SHALL round-trip through non-volatile storage without null-byte truncation.
- If more than `32` matches occur before an FDN collects them, newer matches SHALL overwrite the oldest slots in the `32`-slot ring buffer without failing or blocking gameplay.
- The PDN SHALL persist a separate `collected_high_water_mark` (`0` initially). If `collected_high_water_mark > match_sequence_number` (such as after storage corruption), the PDN SHALL clamp `collected_high_water_mark` to `match_sequence_number`. A retained match with sequence number `N` is pending FDN collection whenever `N > collected_high_water_mark` and `N > match_sequence_number - 32`.
- Advancing `collected_high_water_mark` to `M` marks all matches `<= M` as collected while leaving any newer matches `> M` pending.

#### Scenario: 33rd match overwrites oldest slot without losing newer uncollected matches
- **WHEN** a PDN has 32 uncollected matches (`1..32`) and records a 33rd standalone match before encountering an FDN
- **THEN** match `33` overwrites slot `0`, `match_sequence_number` becomes `33`, and matches `2..33` remain available in ascending order for FDN collection

#### Scenario: Binary UUIDs containing zero bytes round-trip intact through storage
- **WHEN** a match record whose 16-byte match UUID, player UUIDs, or team UUIDs contain `0x00` bytes is written to and read back from non-volatile storage
- **THEN** all 16 bytes of every UUID and all numeric fields read back identically without null-byte truncation

#### Scenario: Record tag is read back with its match record after a restart
- **WHEN** a PDN stores a match record with its record tag and restarts before an FDN collects it
- **THEN** the PDN reads back the same `112` record bytes and the same `16`-byte record tag

#### Scenario: Match record in a slot with no record tag is never sent
- **WHEN** a PDN's pending matches are `3`, in a slot that holds `112` record bytes and no record tag, and `4`, in a slot that holds the record bytes and a record tag, and the PDN hears an FDN's beacon
- **THEN** the PDN sends match `4` with its stored record tag and does not send match `3`, then or later

### Requirement: PDN-to-FDN match transfer, FDN spool bounds, and watermark advancement

A PDN with pending match records SHALL send them to an FDN as `record-relay` requires:

- The PDN SHALL read retained uncollected match records in oldest-first order (from `max(collected_high_water_mark + 1, match_sequence_number - 31)` to `match_sequence_number`) and send them as record frames (`record-relay`) whose record number is the match record's sequence number on that PDN. Each match record SHALL be sent with the record tag stored with it (`record-authentication`), unchanged.
- The PDN's persisted `collected_high_water_mark` SHALL be the mark that the stored acknowledgement advances (`record-relay`); the stored acknowledgement for a match record SHALL advance it to that match's sequence number.
- The FDN SHALL stage ingested PDN match records in its record store (`record-relay`), retaining up to `64` match records awaiting server upload. Acceptance into the record store, the stored acknowledgement, duplicates and a full record store SHALL follow `record-relay`; while the record store is full of match records the PDN retains the uncollected records in its own 32-slot ring buffer until the FDN uploads its stored match records to the server.
- If the PDN loses contact with the FDN before all pending matches are acknowledged by the FDN, `collected_high_water_mark` SHALL reflect the last acknowledged match so only the remaining uncollected matches are sent on the next FDN contact.

#### Scenario: Partial FDN collection advances watermark only for acknowledged matches
- **WHEN** a PDN has uncollected matches `1..5` and unplugs from a Connected FDN after the FDN acknowledges matches `1`, `2`, and `3`
- **THEN** the PDN's `collected_high_water_mark` advances to `3`, and on the next FDN contact the PDN transmits only matches `4` and `5`

#### Scenario: Spoofed wireless acknowledgement from uncabled sender is ignored
- **WHEN** a PDN with uncollected matches `1..5` sends match `1` to an FDN and receives an acknowledgement for match `1` from a device other than the FDN the record was sent to
- **THEN** the PDN ignores the acknowledgement and leaves `collected_high_water_mark` unchanged

#### Scenario: PDN not Connected on any jack delivers pending matches after a beacon
- **WHEN** a PDN with uncollected matches `1..3` and nothing Connected on either serial jack hears an FDN's beacon, and that FDN's stored acknowledgements for matches `1`, `2`, and `3` arrive in turn
- **THEN** the PDN sends matches `1`, `2`, and `3` in ascending order as record frames with record numbers `1`, `2`, and `3`, and its `collected_high_water_mark` advances to `3`

#### Scenario: Stored match record is sent with its stored tag after a restart
- **WHEN** a PDN that stored match `6` with its record tag restarts, holds no record key, and hears an FDN's beacon before any player has registered on it
- **THEN** the PDN sends match `6` with the record tag stored when the match ended

### Requirement: FDN protobuf batch encoding and upload to server

An FDN holding ingested match records SHALL upload them to the central server in batches over HTTPS:

- Each upload batch SHALL contain between `1` and `100` match records; a request with `0` or more than `100` matches SHALL be rejected locally without queuing an HTTP request.
- The FDN SHALL encode the batch via the quickdraw-match protobuf codec as a `POST /quickdraw-matches` request with content type `application/protobuf`.
- For each match record in the batch, the codec SHALL write:
  - the match UUID, both player UUIDs (`player_a_id` and `player_b_id`), and the winner UUID (`winner_player_id`) formatted as lowercase `8-4-4-4-12` hexadecimal text;
  - each player's team UUID (`player_a_team_id`, `player_b_team_id`) as lowercase `8-4-4-4-12` hexadecimal text if and only if that team UUID is not nil;
  - both players' raw reaction times (`player_a_draw_time_ms`, `player_b_draw_time_ms`) as unsigned 64-bit millisecond values (`0..INT64_MAX`) and the UTC occurrence timestamp (`occurred_at_unix_ms`), marked present on the wire even when their numeric value is `0`.
- Each uploaded match SHALL also carry the `112` match record bytes and the `16`-byte record tag exactly as the FDN stored them, alongside the existing fields. It SHALL also carry the sending PDN's MAC address as a troubleshooting note only, as `record-relay` requires ("Sender's MAC address on a match upload is a note only").
- In an upload cycle the FDN SHALL send the match batch before any crash record (`crash-reporting`).

#### Scenario: Batch of matches is encoded and queued to POST /quickdraw-matches with raw reaction times and effective-time winner
- **WHEN** an FDN queues an upload of 2 collected match records (including a match won via posse boost where the winner's raw reaction time is `210 ms` against the opponent's `195 ms`, and one nil team UUID)
- **THEN** the FDN sends `POST /quickdraw-matches` with `application/protobuf`, both players' unsigned raw reaction times (`210 ms` and `195 ms`) and the posse-boosted winner's `winner_player_id` explicitly present on the wire, and the nil team UUID omitted

#### Scenario: Batch count outside 1..100 is rejected without queuing HTTP
- **WHEN** a caller attempts to encode a match upload batch with `0` or `101` records
- **THEN** the request is rejected locally and no HTTP request is queued

#### Scenario: Uploaded match carries the record bytes, the tag and the sender note alongside the decoded fields
- **WHEN** an FDN uploads a match record it stored from a record frame sent by the PDN with MAC address `aa:bb:cc:dd:ee:ff`
- **THEN** the uploaded match carries, in addition to the decoded match fields, the `112` match record bytes and the `16`-byte record tag exactly as the FDN received them, and that MAC address as a troubleshooting note
