# match-collection Specification

## Purpose

PDNs do not connect to Wi-Fi and cannot upload completed 1v1 Quickdraw matches directly to the central server. Instead, each PDN persists its completed standalone matches in a bounded non-volatile ring buffer with a persistent collection watermark, transfers uncollected match records over the peer-to-peer radio when Connected to an FDN on a serial jack, and relies on the FDN to batch-upload collected matches to the server via the protobuf match endpoint (`POST /quickdraw-matches`). The `match-collection` capability defines the match record schema, bounded PDN ring buffer and watermark, PDN-to-FDN match transfer, bounded FDN spool, FDN protobuf batch upload, and per-entry rejection handling.

This specification states the required outcomes, invariants, and environmental constraints of match storage, collection, and server upload.

Terms used throughout:

- **Match record**: the durable record of a completed standalone 1v1 match, containing the 16-byte match UUID, both players' 16-byte UUIDs, both players' 16-byte team UUIDs (or nil UUID when unassigned), the 16-byte winner player UUID (determined from the players' posse-boosted effective draw times), both players' raw reaction times in milliseconds, and the UTC occurrence timestamp in epoch milliseconds.
- **Match sequence number**: a monotonically increasing 1-based unsigned integer assigned to each standalone match persisted on a PDN.
- **Collected high-water mark**: the highest match sequence number confirmed ingested by an FDN from a PDN.

## Requirements

### Requirement: Bounded persistent match ring buffer and collection watermark on PDN

Every completed standalone 1v1 match on a PDN SHALL be persisted in a bounded non-volatile ring buffer in binary form alongside a monotonic sequence counter and collection watermark:

- Each persisted standalone match SHALL increment the PDN's persisted `match_sequence_number` by `1` (starting at `1`) and write the binary match record into a `32`-slot circular buffer indexed by `(match_sequence_number - 1) mod 32`.
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

### Requirement: PDN-to-FDN match transfer, FDN spool bounds, and watermark advancement

When a PDN with pending match records is Connected to an FDN on a physical serial jack:

- The PDN SHALL read retained uncollected match records in oldest-first order (from `max(collected_high_water_mark + 1, match_sequence_number - 31)` to `match_sequence_number`) and transmit them to the cabled FDN's verified MAC address over the peer-to-peer radio.
- The PDN SHALL accept match ingestion acknowledgements only from the verified MAC address of an FDN currently Connected on a serial jack (ignoring wireless acknowledgements from any uncabled sender MAC) and SHALL advance its persisted `collected_high_water_mark` to a match's sequence number only after that cabled FDN acknowledges ingesting the record.
- The FDN SHALL stage ingested PDN match records in a capacity-bounded non-volatile spool (retaining up to `64` match records awaiting server upload). If the FDN's spool is full, the FDN SHALL withhold ingestion acknowledgement so the PDN retains the uncollected records in its own 32-slot ring buffer until the FDN drains its spool to the server.
- If the PDN disconnects before all pending matches are acknowledged by the FDN, `collected_high_water_mark` SHALL reflect the last acknowledged match so only the remaining uncollected matches are sent on the next FDN contact.

#### Scenario: Partial FDN collection advances watermark only for acknowledged matches
- **WHEN** a PDN has uncollected matches `1..5` and unplugs from a Connected FDN after the FDN acknowledges matches `1`, `2`, and `3`
- **THEN** the PDN's `collected_high_water_mark` advances to `3`, and on the next FDN contact the PDN transmits only matches `4` and `5`

#### Scenario: Spoofed wireless acknowledgement from uncabled sender is ignored
- **WHEN** a PDN with uncollected matches `1..5` receives a match ingestion acknowledgement from a wireless sender MAC that is not Connected as an FDN on a serial jack
- **THEN** the PDN ignores the acknowledgement and leaves `collected_high_water_mark` unchanged

### Requirement: FDN protobuf batch encoding and upload to server

An FDN holding ingested match records SHALL upload them to the central server in batches over authenticated HTTPS (`Authorization: Bearer <token>`):

- Each upload batch SHALL contain between `1` and `100` match records; a request with `0` or more than `100` matches SHALL be rejected locally without queuing an HTTP request.
- The FDN SHALL encode the batch via the quickdraw-match protobuf codec as a `POST /quickdraw-matches` request with content type `application/protobuf`.
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
