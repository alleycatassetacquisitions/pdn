## ADDED Requirements

### Requirement: State base class exposes isPreemptable() defaulting to true
The `State` base class SHALL add `virtual bool isPreemptable() const` with a default return value of `true`. States that do not need to block global events require no override.

#### Scenario: State does not override isPreemptable
- **WHEN** the device-level handler checks `isPreemptable()` on a state that has not overridden it
- **THEN** it returns `true` and the event is applied immediately

### Requirement: Critical states override isPreemptable to return false
States with timing-critical UX (e.g. quickdraw countdown, win/lose reveal) SHALL override `isPreemptable()` to return `false` for the duration of the critical window.

#### Scenario: kMcEvent arrives during quickdraw duel
- **WHEN** a `kMcEvent` packet arrives while the PDN is in the quickdraw duel critical window
- **THEN** `isPreemptable()` returns `false` and the event is queued rather than applied

### Requirement: PDN event queue drains on transition to a preemptable state
The PDN device-level handler SHALL drain the `kMcEvent` queue each time the state machine transitions to a state where `isPreemptable()` returns `true`.

#### Scenario: Queue drains after critical state exits
- **WHEN** the quickdraw duel ends and the state transitions to a preemptable state
- **THEN** all queued `kMcEvent` entries are processed in order, with STALE_AND_DROP events checked against their TTL and discarded if expired

### Requirement: NEVER_DROP events displace oldest STALE_AND_DROP entry on queue overflow
When the `kMcEvent` queue is at maximum capacity and a new `NEVER_DROP` event arrives, it SHALL displace the oldest `STALE_AND_DROP` entry. If no `STALE_AND_DROP` entries exist, the new `NEVER_DROP` event SHALL be rejected and an error logged.

#### Scenario: Queue full, new NEVER_DROP event arrives
- **WHEN** the queue is full and a `NEVER_DROP` event arrives
- **THEN** the oldest `STALE_AND_DROP` entry is removed and the new event is enqueued

#### Scenario: Queue full with only NEVER_DROP events
- **WHEN** the queue is full of `NEVER_DROP` events and a new `NEVER_DROP` event arrives
- **THEN** the new event is rejected and `LOG_E` is emitted

### Requirement: FdnGameState exposes isMqttPreemptable() for MqttManager
The `FdnGameState` base class SHALL expose `virtual bool isMqttPreemptable() const` defaulting to `true`. `MqttManager` SHALL check this before applying incoming MC commands to the active FDN game state.

#### Scenario: MQTT message arrives during non-preemptable FDN state
- **WHEN** an MC command arrives via MQTT and the active `FdnGameState` returns `isMqttPreemptable() = false`
- **THEN** MqttManager queues the message and does not dispatch it until the FDN transitions to a preemptable state
