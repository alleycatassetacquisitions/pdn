## MODIFIED Requirements

### Requirement: PDN-to-FDN crash collection and FDN server upload

Unreported crash records SHALL be collected from PDNs by FDNs over peer-to-peer wireless and uploaded by FDNs to the central server over HTTPS:

- **PDN peer-to-peer reporting to an FDN**:
  - A PDN SHALL send its retained pending crash records in ascending crash-number order (from `max(sent_high_water_mark + 1, crash_sequence_number - 9)` to `crash_sequence_number`) as record frames (`record-relay`) whose record number is the record's `crash_number`.
  - The PDN's persisted `sent_high_water_mark` SHALL be the mark that the stored acknowledgement for a crash record advances (`record-relay`), to that record's crash number (stopping on the first unacknowledged record so earlier records are never skipped).
- **FDN server reporting**:
  - An FDN SHALL stage up to `32` relayed crash records in its relay queue, separately from the FDN's own local 10-slot crash ring buffer. Acceptance, acknowledgement, duplicates and full storage SHALL follow `record-relay`.
  - When an FDN has pending local crash records or queued PDN crash records, the FDN SHALL upload them on its periodic uplink (`fdn-apps`), sequentially within a `15,000 ms` upload timeout window.
  - Each record SHALL be encoded via the crash protobuf codec as a device-log protobuf payload containing the originating device's lowercase colon-separated MAC address (`aa:bb:cc:dd:ee:ff`), `crash_number`, `uptime_ms`, `software_version`, `reset_reason`, and (when present) `program_counter`, `exception_cause`, and `task_name`, and sent via HTTP `POST` to `/device-logs` with content type `application/protobuf`.
  - Each relayed PDN crash record SHALL be uploaded with what `record-relay` requires of an uploaded relayed record, alongside these device-log fields.
  - Upon each successful response for an FDN's own crash record, the FDN SHALL advance its local `sent_high_water_mark` to that record's crash number; upon each successful response for a relayed PDN crash record, the FDN SHALL retire that record from its relay queue and queue the next pending record.
  - Upon an HTTP `422` response for a relayed PDN crash record, the FDN SHALL retire that record from its relay queue and continue the upload with the next pending record.
  - Upon an encode error or any other HTTP error, the FDN SHALL stop uploading further records on the current cycle and leave the failed record (and any subsequent records) pending.
  - Once all queued HTTP requests have completed, or when the `15,000 ms` upload timeout expires, the FDN SHALL re-enable the peer-to-peer radio.

#### Scenario: PDN transfers pending crash records to Connected FDN and advances watermark on confirmation
- **WHEN** a PDN with pending crash records `1` and `2` becomes Connected on an FDN's jack and that FDN's stored acknowledgement arrives for both records
- **THEN** the PDN sends record `1` followed by record `2` as record frames to that FDN and advances its `sent_high_water_mark` to `2`

#### Scenario: FDN sequential server upload advances high-water mark and restores peer-to-peer radio
- **WHEN** an FDN has local crash records `1` and `2` pending upload and the server responds with HTTP 204 to both `POST /device-logs` requests within `15,000 ms`
- **THEN** record `1` is uploaded first, `sent_high_water_mark` advances to `1`, record `2` is uploaded next, `sent_high_water_mark` advances to `2`, and the FDN re-enables the peer-to-peer radio once the request queue drains

#### Scenario: Unreachable server on FDN times out after 15 seconds and restores peer-to-peer radio
- **WHEN** an FDN attempts to upload pending crash records and the server is unreachable for `15,000 ms`
- **THEN** the upload timeout expires, unconfirmed records remain pending for a subsequent upload cycle, and the FDN re-enables the peer-to-peer radio

#### Scenario: PDN not Connected on any jack delivers pending crash records after hearing a beacon
- **WHEN** a PDN with `sent_high_water_mark` `0` and pending crash records `1` and `2`, not Connected on any jack, hears an FDN's beacon and that FDN's stored acknowledgement arrives for both records
- **THEN** the PDN sends record `1` followed by record `2` as record frames to that FDN and advances its `sent_high_water_mark` to `2`

#### Scenario: Server 422 retires a relayed crash record and the upload continues
- **WHEN** an FDN uploads relayed PDN crash records `A` then `B` and the server responds with HTTP `422` to the `POST /device-logs` request for record `A`
- **THEN** the FDN retires record `A` from its relay queue without retrying it and uploads record `B` next within the same upload timeout window
