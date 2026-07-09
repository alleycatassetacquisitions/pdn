## ADDED Requirements

### Requirement: kMcEvent is a new ESP-NOW packet type for MC-originated broadcasts
`kMcEvent` SHALL be added to the `PktType` registry in `peer-comms-types.hpp`. It SHALL carry an `McEventPayload` containing the event type, drop policy, TTL, and event-specific data.

#### Scenario: FDN broadcasts a kMcEvent to all nearby PDNs
- **WHEN** MqttManager relays an MC broadcast
- **THEN** it sends a `kMcEvent` packet via ESP-NOW broadcast (not unicast) so all nearby PDNs receive it regardless of jack connection

### Requirement: McEventPayload carries DropPolicy and ttlMs specified by MC at publish time
`McEventPayload` SHALL contain a `DropPolicy dropPolicy` field (STALE_AND_DROP | NEVER_DROP) and a `uint32_t ttlMs` field. `ttlMs` SHALL be ignored when `dropPolicy` is `NEVER_DROP`.

#### Scenario: MC sends a time-limited easter egg event
- **WHEN** MC publishes an event with `dropPolicy = STALE_AND_DROP` and `ttlMs = 30000`
- **THEN** the FDN relays it with those fields intact and the PDN uses them for queue staleness checks

#### Scenario: MC sends a critical never-drop event
- **WHEN** MC publishes an event with `dropPolicy = NEVER_DROP`
- **THEN** the PDN applies it regardless of how long it waited in the queue

### Requirement: TTL is measured from the time of PDN ESP-NOW receipt
The staleness check on the PDN SHALL use `millis() - receivedAtMs > ttlMs`, where `receivedAtMs` is the time the PDN received the `kMcEvent` packet. No clock synchronisation between MC, FDN, and PDN is required.

#### Scenario: Event drains from queue after TTL expires
- **WHEN** a STALE_AND_DROP event has waited in the PDN queue longer than its `ttlMs`
- **THEN** it is discarded without being applied when the queue drains

### Requirement: kMcEvent handler is registered at PDN device level, not per-state
The `kMcEvent` handler SHALL be registered once during PDN device initialisation (in `main.cpp` or equivalent device bootstrap), not inside any individual state.

#### Scenario: PDN receives kMcEvent while in any app
- **WHEN** a `kMcEvent` packet arrives while the PDN is in any state (Quickdraw, Controller, Idle, etc.)
- **THEN** the device-level handler receives it and decides whether to apply or queue based on the current state's preemptability
