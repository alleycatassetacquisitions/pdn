## ADDED Requirements

### Requirement: Esp32S3MQTTDriver implements MQTTInterface with persistent session
`Esp32S3MQTTDriver` SHALL implement `MQTTInterface` and connect to a broker using a stable `clientId` derived from the device MAC address. It SHALL connect with `cleanSession = false` to enable persistent session behaviour at the broker.

#### Scenario: Driver connects with persistent session
- **WHEN** `connect()` is called
- **THEN** the driver establishes a TCP connection to the configured broker, sends a CONNECT packet with `cleanSession = false` and a MAC-derived `clientId`, and calls `onConnect()` on success

### Requirement: Driver supports QoS 1 publish and subscribe
The driver SHALL publish messages with QoS 1 (at-least-once delivery) and subscribe to topics with QoS 1.

#### Scenario: Driver publishes a message
- **WHEN** `publish(topic, payload)` is called while connected
- **THEN** the message is sent with QoS 1 and the driver waits for PUBACK before considering delivery complete

#### Scenario: Driver subscribes to a topic
- **WHEN** `subscribe(topic)` is called
- **THEN** the broker registers the subscription and delivers matching messages to `onMessage(topic, payload)`

### Requirement: Driver disconnects gracefully
The driver SHALL send a MQTT DISCONNECT packet when `disconnect()` is called, preserving the persistent session on the broker so queued QoS 1 messages are held for redelivery on reconnect.

#### Scenario: Graceful disconnect preserves queued messages
- **WHEN** `disconnect()` is called while connected
- **THEN** a DISCONNECT packet is sent, the TCP connection is closed, and the broker retains all queued QoS 1 messages for the client session

### Requirement: Driver reconnects automatically after unexpected disconnection
The driver SHALL detect unexpected disconnections and attempt reconnection with exponential backoff.

#### Scenario: Unexpected disconnection triggers reconnect
- **WHEN** the TCP connection drops without an explicit `disconnect()` call
- **THEN** the driver calls `onDisconnect()` and begins reconnection attempts with exponential backoff
