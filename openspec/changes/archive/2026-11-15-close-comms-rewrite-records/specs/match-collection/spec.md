## MODIFIED Requirements

### Requirement: Bounded persistent match ring buffer and collection watermark on PDN

Every completed standalone 1v1 match between two different non-nil player UUIDs whose winner both devices agree on SHALL be persisted on the PDN in a bounded non-volatile ring buffer in binary form, in its own storage area, alongside a monotonic sequence counter and collection watermark:

- Each persisted match SHALL increment the persisted `match_sequence_number` by `1` (starting at `1`) and write the binary match record, which names both player UUIDs, into a `32`-slot circular buffer indexed by `(match_sequence_number - 1) mod 32`.
- Binary match records containing raw 16-byte UUIDs (which may include `0x00` bytes) SHALL round-trip through non-volatile storage without null-byte truncation.
- While `32` matches are pending, the PDN SHALL NOT persist a new match and SHALL log the refusal; a pending match is never overwritten, and gameplay is not blocked.
- A match with a nil player UUID on either side, the same player UUID on both sides, a shootout match, or a match whose two results disagree on the winner SHALL NOT be persisted.
- The PDN SHALL persist a separate `collected_high_water_mark` (`0` initially), clamped to `match_sequence_number` if larger. Match `N` is pending whenever `N > collected_high_water_mark` and `N > match_sequence_number - 32`. Advancing the mark to `M` marks matches `<= M` as collected and leaves newer matches pending.

#### Scenario: 33rd match overwrites oldest slot without losing newer uncollected matches
- **WHEN** a PDN has 32 pending matches (`1..32`) and finishes a 33rd standalone match before encountering an FDN
- **THEN** the 33rd match is not stored, `match_sequence_number` stays `32`, and matches `1..32` remain pending in ascending order

#### Scenario: Binary UUIDs containing zero bytes round-trip intact through storage
- **WHEN** a match record whose 16-byte match UUID, player UUIDs, or team UUIDs contain `0x00` bytes is written to and read back from non-volatile storage
- **THEN** all 16 bytes of every UUID and all numeric fields read back identically without null-byte truncation

### Requirement: PDN-to-FDN match transfer, FDN spool bounds, and watermark advancement

A PDN with pending match records SHALL deliver them to any FDN it hears by beacon, Connected on a jack or not, as record frames (`record-relay`):

- The PDN SHALL send pending match records oldest first, from `max(collected_high_water_mark + 1, match_sequence_number - 31)` to `match_sequence_number`.
- The PDN SHALL advance its persisted `collected_high_water_mark` to a match's sequence number only on that FDN's stored acknowledgement for the record (`record-relay`), never on an acknowledgement from any other sender.
- The FDN SHALL stage received match records in a capacity-bounded non-volatile store of up to `32` match records awaiting upload. If the store is full, the FDN SHALL withhold the stored acknowledgement so the PDN keeps the records pending.
- If contact ends before all pending matches are acknowledged, `collected_high_water_mark` SHALL reflect the last acknowledged match so only the rest are sent on the next contact.

#### Scenario: Partial FDN collection advances watermark only for acknowledged matches
- **WHEN** a PDN has pending matches `1..5` and leaves an FDN's range after the FDN acknowledges matches `1`, `2`, and `3`
- **THEN** the PDN's `collected_high_water_mark` advances to `3`, and on the next FDN beacon the PDN sends only matches `4` and `5`

#### Scenario: Spoofed wireless acknowledgement from uncabled sender is ignored
- **WHEN** a PDN that sent match `1` to an FDN receives a stored acknowledgement for match `1` from a different sender MAC
- **THEN** the PDN ignores the acknowledgement and leaves `collected_high_water_mark` unchanged

### Requirement: FDN protobuf batch encoding and upload to server

An FDN holding stored match records SHALL upload them to the central server in batches over HTTPS, at boot and every `300,000 ms` while records wait, but not while a jack is Connected, sending its match batch before crash records:

- Each upload batch SHALL contain between `1` and `100` match records; a request with `0` or more than `100` matches SHALL be rejected locally without queuing an HTTP request.
- The FDN SHALL encode the batch via the quickdraw-match protobuf codec as a `POST /quickdraw-matches` request with content type `application/protobuf`. On a `400`, `413` or `415` response it SHALL send the oldest record alone and retire it on any response, so one rejected record cannot block the store.
- For each match record in the batch, the codec SHALL write:
  - the match UUID, both player UUIDs (`player_a_id` and `player_b_id`), and the winner UUID (`winner_player_id`) formatted as lowercase `8-4-4-4-12` hexadecimal text;
  - each player's team UUID (`player_a_team_id`, `player_b_team_id`) as lowercase `8-4-4-4-12` hexadecimal text if and only if that team UUID is not nil;
  - both players' raw reaction times (`player_a_draw_time_ms`, `player_b_draw_time_ms`) as unsigned 64-bit millisecond values (`0..INT64_MAX`) and the UTC occurrence timestamp (`occurred_at_unix_ms`), marked present on the wire even when their numeric value is `0`.

#### Scenario: Batch of matches is encoded and queued to POST /quickdraw-matches with raw reaction times and effective-time winner
- **WHEN** an FDN queues an upload of 2 collected match records (including a match won via posse boost where the winner's raw reaction time is `210 ms` against the opponent's `195 ms`, and one nil team UUID)
- **THEN** the FDN sends `POST /quickdraw-matches` with `application/protobuf`, both players' unsigned raw reaction times (`210 ms` and `195 ms`) and the posse-boosted winner's `winner_player_id` explicitly present on the wire, and the nil team UUID omitted

#### Scenario: Batch count outside 1..100 is rejected without queuing HTTP
- **WHEN** a caller attempts to encode a match upload batch with `0` or `101` records
- **THEN** the request is rejected locally and no HTTP request is queued

## ADDED Requirements

### Requirement: Matches stored by older firmware are kept

A PDN SHALL keep matches stored by older firmware in their original storage unchanged: no path SHALL write, clear or erase them. They SHALL NOT be relayed until the server offers a route for them.

#### Scenario: Update over older-firmware matches
- **WHEN** this firmware first boots on a PDN holding 5 matches stored by older firmware and plays a match
- **THEN** the 5 older matches are still stored byte for byte and none was sent
