## MODIFIED Requirements

### Requirement: PDN-to-FDN crash collection and FDN server upload

Unreported crash records SHALL be relayed from PDNs to FDNs over peer-to-peer wireless and uploaded by FDNs to the central server over HTTPS:

- **PDN relay to an FDN**:
  - A PDN SHALL send its retained pending crash records in ascending crash-number order (from `max(sent_high_water_mark + 1, crash_sequence_number - 9)` to `crash_sequence_number`) as crash record frames to any FDN it hears by beacon, Connected on a jack or not (`record-relay`).
  - The PDN SHALL advance its persisted `sent_high_water_mark` to a record's crash number only on that FDN's stored acknowledgement for it (`record-relay`), so earlier records are never skipped.
- **FDN server reporting**:
  - An FDN SHALL store relayed crash records, up to `16`, with each sender's MAC address and crash number, separately from its own local 10-slot crash ring buffer.
  - At boot and every `300,000 ms` while its own or relayed crash records wait, and not while a jack is Connected, the FDN SHALL upload them sequentially, after its match batch, within a `15,000 ms` upload timeout window.
  - Each record SHALL be encoded via the crash protobuf codec as a device-log protobuf payload containing the originating device's lowercase colon-separated MAC address (`aa:bb:cc:dd:ee:ff`; the sender's MAC for a relayed record), `crash_number`, `uptime_ms`, `software_version`, `reset_reason`, and (when present) `program_counter`, `exception_cause`, and `task_name`, and sent via HTTP `POST` to `/device-logs` with content type `application/protobuf`.
  - Upon each successful response for an FDN's own crash record, the FDN SHALL advance its local `sent_high_water_mark` to that record's crash number; upon each successful response for a relayed crash record, the FDN SHALL retire it from its store and upload the next.
  - Upon an encode or HTTP error, the FDN SHALL stop uploading further records on the current cycle and leave the failed record (and any subsequent records) pending.
  - Once all queued HTTP requests have completed, or when the `15,000 ms` upload timeout expires, the FDN SHALL re-enable the peer-to-peer radio.

#### Scenario: PDN transfers pending crash records to Connected FDN and advances watermark on confirmation
- **WHEN** a PDN with pending crash records `1` and `2` hears the beacon of an FDN, Connected on its jack or not, and the FDN stores and acknowledges both records
- **THEN** the PDN sends record `1`, then record `2` after the first stored acknowledgement, and advances its `sent_high_water_mark` to `2`

#### Scenario: FDN sequential server upload advances high-water mark and restores peer-to-peer radio
- **WHEN** an FDN has local crash records `1` and `2` pending upload and the server responds with HTTP 204 to both `POST /device-logs` requests within `15,000 ms`
- **THEN** record `1` is uploaded first, `sent_high_water_mark` advances to `1`, record `2` is uploaded next, `sent_high_water_mark` advances to `2`, and the FDN re-enables the peer-to-peer radio once the request queue drains

#### Scenario: Unreachable server on FDN times out after 15 seconds and restores peer-to-peer radio
- **WHEN** an FDN attempts to upload pending crash records and the server is unreachable for `15,000 ms`
- **THEN** the upload timeout expires, unconfirmed records remain pending for a subsequent upload cycle, and the FDN re-enables the peer-to-peer radio
