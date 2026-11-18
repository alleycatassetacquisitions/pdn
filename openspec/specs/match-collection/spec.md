# match-collection Specification

## Purpose

PDNs do not connect to Wi-Fi and cannot upload completed 1v1 Quickdraw matches directly to the central server. Instead, each PDN persists its completed standalone matches in a bounded non-volatile ring buffer with a persistent collection watermark, transfers uncollected match records over the peer-to-peer radio when Connected to an FDN on a serial jack, and relies on the FDN to batch-upload collected matches to the server via the protobuf match endpoint (`POST /quickdraw-matches`). The `match-collection` capability defines the match record schema, bounded PDN ring buffer and watermark, PDN-to-FDN match transfer, bounded FDN spool, FDN protobuf batch upload, and per-entry rejection handling.

This specification states the required outcomes, invariants, and environmental constraints of match storage, collection, and server upload.

Terms used throughout:

- **Match record**: the durable record of a completed standalone 1v1 match, containing the 16-byte match UUID, both players' 16-byte UUIDs, both players' 16-byte team UUIDs (or nil UUID when unassigned), both players' 6-byte MAC addresses, the 16-byte winner player UUID (determined from the players' posse-boosted effective draw times), both players' raw reaction times in milliseconds, and the UTC occurrence timestamp in epoch milliseconds.
- **Match sequence number**: a monotonically increasing 1-based unsigned integer assigned to each standalone match persisted on a PDN.
- **Collected high-water mark**: the highest match sequence number confirmed ingested by an FDN from a PDN.

## Requirements

### Requirement: Bounded persistent match ring buffer and collection watermark on PDN

Every completed standalone 1v1 match SHALL be persisted on the PDN in a bounded non-volatile ring buffer in binary form, in its own storage area, alongside a monotonic sequence counter and collection watermark:

- Each persisted match SHALL increment the persisted `match_sequence_number` by `1` (starting at `1`) and write the binary match record, which names both player UUIDs, into a `32`-slot circular buffer indexed by `(match_sequence_number - 1) mod 32`.
- Binary match records containing raw 16-byte UUIDs (which may include `0x00` bytes) SHALL round-trip through non-volatile storage without null-byte truncation.
- While `32` matches are pending, the PDN SHALL NOT persist a new match and SHALL log the refusal; a pending match is never overwritten, and gameplay is not blocked.
- A shootout match SHALL NOT be persisted. Every other finalized match SHALL be persisted as recorded, including one with a nil player UUID, the same player UUID on both sides, or no agreed winner; the server rejects or flags those.
- The PDN SHALL persist a separate `collected_high_water_mark` (`0` initially), clamped to `match_sequence_number` if larger. Match `N` is pending whenever `N > collected_high_water_mark` and `N > match_sequence_number - 32`. Advancing the mark to `M` marks matches `<= M` as collected and leaves newer matches pending.

#### Scenario: 33rd match is refused while 32 are pending
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

#### Scenario: Acknowledgement from a different sender is ignored
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

### Requirement: Server batch acceptance and per-entry rejection handling

When the FDN receives a response (bounded in size so an oversized response body is rejected without exhausting memory) to `POST /quickdraw-matches`:

- **Full acceptance (`204 No Content` or empty `200 OK` body)**:
  - All matches in the submitted batch are confirmed stored by the server; the FDN SHALL remove the uploaded batch from its pending upload spool.
- **Partial acceptance with per-entry rejections (non-empty `200 OK` body)**:
  - The FDN SHALL validate and decode the protobuf rejection list in response order, where each rejection carries `match_index`, `match_id` (`8-4-4-4-12` hexadecimal UUID text), up to `2` offending field names, and a rejection error code (mapping unrecognized future numeric codes to an unknown-error placeholder while preserving the raw numeric code).
  - Because the server commits all valid entries in the batch via per-match savepoints and permanently rejects the listed entries, the FDN SHALL retire the processed batch from its pending upload spool so rejected entries do not wedge or poison subsequent uploads, and SHALL log or surface the rejected entries.
- **Undecodable response or transport/batch-wide error (network error, timeout, `409 Conflict` when no active event is running, or `5xx`)**:
  - If a non-empty response body is malformed (such as a non-UUID `match_id` or more than `2` field names in a rejection) or the HTTP request fails with a transport or batch-wide error, the FDN SHALL retain the entire batch in its spool for retry on a subsequent upload cycle.

#### Scenario: Partial rejection retires processed batch without poisoning future uploads
- **WHEN** an FDN uploads a batch of 10 matches and the server responds with `200 OK` listing 1 rejected match (`SAME_PLAYER` on `player_a_id` and `player_b_id`)
- **THEN** the FDN decodes and logs the rejection, retires the processed batch so the 1 rejected entry is not re-sent endlessly, and continues uploading subsequent batches

#### Scenario: Transient network or 5xx failure retains batch for retry
- **WHEN** an FDN's `POST /quickdraw-matches` request times out or receives an HTTP 503 error
- **THEN** the FDN retains all matches in the batch for the next upload cycle

### Requirement: Matches stored by older firmware are kept

A PDN SHALL keep matches stored by older firmware in their original storage unchanged: no path SHALL write, clear or erase them. They SHALL NOT be relayed until the server offers a route for them.

#### Scenario: Update over older-firmware matches
- **WHEN** this firmware first boots on a PDN holding 5 matches stored by older firmware and plays a match
- **THEN** the 5 older matches are still stored byte for byte and none was sent
