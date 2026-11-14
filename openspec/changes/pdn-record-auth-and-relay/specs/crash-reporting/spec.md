## MODIFIED Requirements

### Requirement: PDN-to-FDN crash collection and FDN server upload

Unreported crash records SHALL be collected from PDNs by FDNs over peer-to-peer wireless and uploaded by FDNs to the central server over HTTPS:

- **PDN peer-to-peer reporting to an FDN**:
  - A PDN SHALL send its retained pending crash records in ascending crash-number order (from `max(sent_high_water_mark + 1, crash_sequence_number - 9)` to `crash_sequence_number`) as record frames (`record-relay`) whose record number is the record's `crash_number`. A crash record SHALL be sent with no record tag (`record-authentication`), whether or not the PDN holds a record key.
  - The PDN's persisted `sent_high_water_mark` SHALL be the mark that the stored acknowledgement for a crash record advances (`record-relay`), to that record's crash number (stopping on the first unacknowledged record so earlier records are never skipped).
- **FDN server reporting**:
  - An FDN SHALL stage up to `32` relayed crash records in its record store (`record-relay`), separately from the FDN's own local 10-slot crash ring buffer. Acceptance, acknowledgement, duplicates and full storage SHALL follow `record-relay`.
  - When an FDN has pending local crash records or relayed PDN crash records in its record store, the FDN SHALL upload them at boot and on its periodic uplink (`fdn-apps`), in one upload cycle that sends everything waiting, sequentially within a `15,000 ms` upload timeout window: the match batch (`match-collection`) first, so that a long crash queue cannot starve match records inside the window, then its own crash records, then the relayed records.
  - Each record SHALL be encoded via the crash protobuf codec as a device-log protobuf payload containing the originating device's lowercase colon-separated MAC address (`aa:bb:cc:dd:ee:ff`), `crash_number`, `uptime_ms`, `software_version`, `reset_reason`, and (when present) `program_counter`, `exception_cause`, and `task_name`, and sent via HTTP `POST` to `/device-logs` with content type `application/protobuf`. For a relayed PDN crash record the originating device is the PDN that sent it (`record-relay`), and the payload SHALL carry no other field.
  - Upon each successful response for an FDN's own crash record, the FDN SHALL advance its local `sent_high_water_mark` to that record's crash number; upon each successful response for a relayed PDN crash record, the FDN SHALL retire that record from its record store and queue the next pending record.
  - Upon an HTTP `400`, `413`, `415` or `422` response for a relayed PDN crash record, which says the request itself can never be accepted, the FDN SHALL retire that record from its record store and continue the upload with the next pending record. This SHALL NOT apply to the FDN's own crash records.
  - Upon an encode error or any other HTTP error, the FDN SHALL stop uploading further records of that group (its own, or relayed) on the current cycle and leave the failed record (and any subsequent records of that group) pending. An own crash record whose `sent_high_water_mark` cannot be advanced after a successful response SHALL count as such a failure. A failure on one of the FDN's own crash records SHALL NOT stop its relayed PDN crash records from uploading on that cycle.
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

#### Scenario: Relayed crash record is uploaded under the originating PDN's MAC address
- **WHEN** an FDN uploads a crash record it stored from a record frame sent by the PDN with MAC address `aa:bb:cc:dd:ee:ff`
- **THEN** the device-log payload names `aa:bb:cc:dd:ee:ff` as the device, carries the record's decoded fields, and carries no record tag

#### Scenario: Relayed crash records upload on the periodic uplink
- **WHEN** an FDN that has uploaded at boot stores a relayed PDN crash record and its periodic uplink next runs
- **THEN** the FDN uploads that record on that uplink

#### Scenario: An answer that the request can never be accepted retires a relayed crash record and the upload continues
- **WHEN** an FDN uploads relayed PDN crash records `A` then `B` and the server responds with HTTP `400`, `413`, `415` or `422` to the `POST /device-logs` request for record `A`
- **THEN** the FDN retires record `A` from its record store without retrying it and uploads record `B` next within the same upload timeout window

#### Scenario: Any other failure keeps a relayed crash record for the next cycle
- **WHEN** an FDN uploads relayed PDN crash records `A` then `B` and the `POST /device-logs` request for record `A` receives HTTP `503`
- **THEN** the FDN stops uploading relayed records for the current cycle and both records stay in its record store

#### Scenario: A failure on the FDN's own crash record does not stop relayed records
- **WHEN** an FDN with its own pending crash record `1` and relayed PDN crash record `A` uploads, and the server responds with HTTP `400` to the request for record `1`
- **THEN** the FDN leaves its own record `1` pending with `sent_high_water_mark` unchanged and uploads relayed record `A` on the same cycle

#### Scenario: One upload cycle sends the match batch, then own crash records, then relayed crash records
- **WHEN** an FDN holding stored match records, its own pending crash record `1` and relayed PDN crash record `A` starts an upload cycle and the server accepts every request
- **THEN** the FDN sends the match batch to `POST /quickdraw-matches` first, then its own record `1`, then relayed record `A`, all within the same `15,000 ms` window

#### Scenario: An own crash record whose mark cannot move does not stop relayed records
- **WHEN** the server accepts an FDN's own crash record `1`, the FDN cannot advance `sent_high_water_mark` to `1`, and relayed PDN crash record `A` is pending
- **THEN** the FDN does not upload record `1` again on that cycle and uploads relayed record `A` on the same cycle
