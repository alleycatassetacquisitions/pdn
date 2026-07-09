## ADDED Requirements

### Requirement: MqttManager maintains a topic subscription table with typed handlers
`MqttManager` SHALL maintain a table mapping topic strings to handler callbacks. Incoming messages SHALL be dispatched to the registered handler for the matching topic.

#### Scenario: Incoming message is dispatched
- **WHEN** an MQTT message arrives on a subscribed topic
- **THEN** MqttManager dispatches it to the registered handler for that topic

#### Scenario: Message arrives with no handler registered
- **WHEN** an MQTT message arrives on a topic with no registered handler
- **THEN** the message is silently dropped and a debug log is emitted

### Requirement: MqttManager checks FdnGameState preemptability before applying incoming messages
Before applying an incoming MC command to the active FDN state, `MqttManager` SHALL check `isMqttPreemptable()` on the current state. If not preemptable, the message SHALL be queued for application on the next preemptable state.

#### Scenario: Message arrives during non-preemptable state
- **WHEN** an incoming MC command arrives and the current FDN state returns `isMqttPreemptable() = false`
- **THEN** the message is held in a local queue and not dispatched to the handler

#### Scenario: State becomes preemptable
- **WHEN** the FDN transitions to a state where `isMqttPreemptable() = true`
- **THEN** all queued messages are dispatched in order

### Requirement: MqttManager relays MC events downstream via ESP-NOW broadcast
When `MqttManager` receives a topic designated for PDN relay (e.g. `pdn/broadcast/event`, `pdn/fdn/{fdnId}/pdns/render`), it SHALL forward the payload as a `kMcEvent` ESP-NOW broadcast to all nearby PDNs.

#### Scenario: MC pushes a broadcast event
- **WHEN** MqttManager receives a message on `pdn/broadcast/event`
- **THEN** it constructs a `kMcEvent` packet and broadcasts it over ESP-NOW ch 6

### Requirement: MqttManager relays PDN-originated events upstream via MQTT publish
When the FDN receives a PDN event that requires upstream delivery (e.g. match result, player registration), `MqttManager` SHALL publish it to the appropriate upstream topic.

#### Scenario: PDN match result relayed upstream
- **WHEN** the FDN receives a match result event from a PDN via ESP-NOW
- **THEN** MqttManager publishes it to `pdn/fdn/{fdnId}/relay` with QoS 1

### Requirement: MqttManager exposes disconnectGracefully and reconnect for RadioManager
`MqttManager` SHALL expose `disconnectGracefully()` and `reconnect()` methods for `RadioManager` to call during mode transitions.

#### Scenario: RadioManager requests graceful disconnect before ESP-NOW mode
- **WHEN** `disconnectGracefully()` is called
- **THEN** MqttManager calls `Esp32S3MQTTDriver::disconnect()` and signals completion to RadioManager
