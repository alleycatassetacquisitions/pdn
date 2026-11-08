# crash-reporting Specification

## Purpose

PDN and FDN devices operating in the field can experience unexpected faults (panics, watchdog timeouts, brownouts, or CPU lockups) without a bench monitor attached. The crash-reporting capability captures the fault context of an abnormal reset on the subsequent boot, retains a bounded persistent history in non-volatile storage, exposes the history over the USB diagnostic interface, and transmits unreported crash records — from a PDN over peer-to-peer wireless to a Connected FDN (without using Wi-Fi on a PDN), and from an FDN to the central server.

This specification states the required outcomes, invariants, and environmental constraints of crash capture, storage, and reporting.

Terms used throughout:

- **Crash sequence number**: a monotonically increasing 1-based unsigned integer assigned to each captured crash on a device.
- **Sent high-water mark**: the highest local crash sequence number confirmed transmitted by the device.
- **Crashed-boot uptime**: the elapsed milliseconds the previous boot had run as of its last main-loop pass before the abnormal reset.
- **Core-dump summary**: the post-mortem fault metadata (crashed task name, program counter, and exception cause) written to flash by the platform panic handler prior to reboot.

## Requirements

### Requirement: Crashed-boot uptime preservation across resets

The device SHALL continuously record its current uptime in milliseconds into reset-surviving memory on every main-loop pass alongside a validity marker:

- At the start of boot, before any subsystem initializes, the device SHALL read and clear the validity marker.
- If the validity marker is present, the stored millisecond value SHALL be taken as the crashed-boot uptime for any crash captured on this boot.
- If the validity marker is absent (such as after a cold power-on), the crashed-boot uptime SHALL be `0`.

#### Scenario: Panic restart preserves uptime from last main-loop pass
- **WHEN** a device running for 42,500 ms experiences a panic reset and reboots without losing power
- **THEN** the boot sequence reads a crashed-boot uptime of `42500` ms and clears the marker before the main loop starts

#### Scenario: Cold power-on reports zero crashed-boot uptime
- **WHEN** a device boots from a cold power-on where reset-surviving memory contains uninitialized data
- **THEN** the validity marker check fails and the crashed-boot uptime is `0`

### Requirement: Abnormal reset classification and core-dump extraction

At boot, after non-volatile storage is initialized, the device SHALL inspect the platform reset reason and check whether a core-dump summary is present in flash:

- A crash record SHALL be captured if and only if a core-dump summary is present in flash OR the hardware reset reason is one of: panic, interrupt watchdog, task watchdog, other hardware watchdog, brownout, or CPU lockup.
- Clean resets (power-on, external reset pin, deliberate software restart without a core dump, deep sleep wake, USB reset, or JTAG reset) SHALL NOT produce a crash record.
- When a core-dump summary is present, has a non-empty task name, and carries a valid exception cause, the crash record SHALL include the crashed task name (truncated to at most `15` ASCII characters) and the 32-bit program counter.
- For any captured core-dump summary whose reset reason is not task watchdog, the crash record SHALL also include the 32-bit exception cause (including cause `0`, `IllegalInstruction`). For a task-watchdog reset, the exception cause SHALL be omitted because the saved interrupt frame carries no exception cause.
- Every crash record SHALL include the firmware commit hash (up to `8` ASCII characters, or `"unknown"` when built without commit metadata).
- After inspecting a core-dump summary, the device SHALL erase the core-dump flash partition so that a subsequent clean reset cannot re-ingest the same core dump.

#### Scenario: Deliberate software restart without core dump is ignored
- **WHEN** the device restarts for an OTA firmware switch or deliberate reboot and no core-dump summary exists in flash
- **THEN** no crash record is created and `crash_sequence_number` is unchanged

#### Scenario: Panic with core dump records task, program counter, and cause zero
- **WHEN** the device reboots from an `abort()` panic that wrote a core-dump summary with exception cause `0` (`IllegalInstruction`), program counter `0x42001234`, and task `"main"`
- **THEN** a new crash record is persisted containing task `"main"`, program counter `0x42001234`, exception cause `0`, and the core-dump partition is erased

#### Scenario: Task watchdog reset omits exception cause
- **WHEN** the device reboots from a task-watchdog reset with a core-dump summary for task `"loopTask"` at program counter `0x42005678`
- **THEN** the captured crash record includes task `"loopTask"` and program counter `0x42005678`, and omits the exception cause

### Requirement: Bounded persistent ring buffer and sequence tracking

Captured crash records SHALL be persisted in non-volatile storage under a dedicated namespace:

- Each captured crash SHALL increment the persisted `crash_sequence_number` by `1` (starting at `1` for the first crash ever recorded on the device).
- Records SHALL be stored in a `10`-slot circular buffer indexed by `crash_sequence_number mod 10`, so at most the `10` most recent local crash records are retained in storage at any time.
- The device SHALL persist a separate `sent_high_water_mark` (`0` initially). If `sent_high_water_mark > crash_sequence_number` (such as after storage corruption), the device SHALL clamp `sent_high_water_mark` to `crash_sequence_number`. When scanning for pending local crash records, the device SHALL start at `max(sent_high_water_mark + 1, crash_sequence_number - 9)` so a large gap between `sent_high_water_mark` and `crash_sequence_number` never scans more than the `10` retained slots.
- When loading slot `N mod 10` for sequence number `N`, if the slot is empty, corrupt, or holds a record whose stored crash number does not equal `N` (because a later crash overwrote that ring-buffer slot), the device SHALL treat crash `N` as no longer retained and advance `sent_high_water_mark` past `N` without transmitting it.

#### Scenario: Eleventh crash overwrites the first slot while preserving monotonic sequence
- **WHEN** a device has already recorded 10 crashes (`1..10`) and captures an 11th crash
- **THEN** crash `11` is written to slot `11 mod 10 = 1`, `crash_sequence_number` becomes `11`, and scanning pending records starts at `max(sent_high_water_mark + 1, 2)`

### Requirement: PDN-to-FDN crash collection and FDN server upload

Unreported crash records SHALL be collected from PDNs by FDNs over peer-to-peer wireless and uploaded by FDNs to the central server over authenticated HTTPS (`Authorization: Bearer <token>`):

- **PDN peer-to-peer reporting to a Connected FDN**:
  - A PDN SHALL transfer its retained pending crash records sequentially in ascending crash-number order (from `max(sent_high_water_mark + 1, crash_sequence_number - 9)` to `crash_sequence_number`) over unicast on the crash-log packet type to an FDN Connected on a serial jack, including the PDN's 6-byte `device_mac` and the record's `crash_number`.
  - The PDN SHALL advance its persisted `sent_high_water_mark` to a record's crash number only when the Connected FDN confirms ingestion of that record (stopping on the first unconfirmed record so earlier records are never skipped).
- **FDN server reporting**:
  - An FDN SHALL stage crash records collected from PDNs in a bounded relay queue (preserving each originating PDN's `device_mac` and `crash_number` separately from the FDN's own local 10-slot crash ring buffer).
  - When an FDN has pending local crash records or queued PDN crash records, the FDN SHALL upload them sequentially within a `15,000 ms` upload timeout window.
  - Each record SHALL be encoded via the crash protobuf codec as a device-log protobuf payload containing the originating device's lowercase colon-separated MAC address (`aa:bb:cc:dd:ee:ff`), `crash_number`, `uptime_ms`, `software_version`, `reset_reason`, and (when present) `program_counter`, `exception_cause`, and `task_name`, and sent via HTTP `POST` to `/device-logs` with content type `application/protobuf`.
  - Upon each successful response for an FDN's own crash record, the FDN SHALL advance its local `sent_high_water_mark` to that record's crash number; upon each successful response for a relayed PDN crash record, the FDN SHALL retire that record from its relay queue and queue the next pending record.
  - Upon an encode or HTTP error, the FDN SHALL stop uploading further records on the current cycle and leave the failed record (and any subsequent records) pending.
  - Once all queued HTTP requests have completed, or when the `15,000 ms` upload timeout expires, the FDN SHALL re-enable the peer-to-peer radio.

#### Scenario: PDN transfers pending crash records to Connected FDN and advances watermark on confirmation
- **WHEN** a PDN with pending crash records `1` and `2` connects to an FDN on a serial jack and the FDN confirms ingestion of both records
- **THEN** the PDN transmits record `1` followed by record `2` over peer-to-peer wireless with its own `device_mac` and advances its `sent_high_water_mark` to `2`

#### Scenario: FDN sequential server upload advances high-water mark and restores peer-to-peer radio
- **WHEN** an FDN has local crash records `1` and `2` pending upload and the server responds with HTTP 204 to both `POST /device-logs` requests within `15,000 ms`
- **THEN** record `1` is uploaded first, `sent_high_water_mark` advances to `1`, record `2` is uploaded next, `sent_high_water_mark` advances to `2`, and the FDN re-enables the peer-to-peer radio once the request queue drains

#### Scenario: Unreachable server on FDN times out after 15 seconds and restores peer-to-peer radio
- **WHEN** an FDN attempts to upload pending crash records and the server is unreachable for `15,000 ms`
- **THEN** the upload timeout expires, unconfirmed records remain pending for a subsequent upload cycle, and the FDN re-enables the peer-to-peer radio

### Requirement: USB diagnostic crash-log dump

On every main-loop pass, the device SHALL poll the USB diagnostic serial interface for the case-insensitive line command `CRASHLOG`:

- When `CRASHLOG` is received, the device SHALL write a human-readable summary to the USB diagnostic interface showing the total device crash count, the number of pending records, and one line per retained record in the 10-slot ring buffer (including whether each record is `sent` or `pending`, its numeric and symbolic reset reason, crashed-boot uptime, core-dump details when present, and firmware commit hash).

#### Scenario: Bench operator sends CRASHLOG over USB serial
- **WHEN** `CRASHLOG` is received on the USB diagnostic serial interface of a device with stored crash records
- **THEN** the device prints the crash count, pending count, and each retained crash record in the ring buffer
