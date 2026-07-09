## 1. Rename and Refactor RadioManager

- [ ] 1.1 Rename `WirelessManager` → `RadioManager` in all headers, source files, and instantiation sites
- [ ] 1.2 Add `requestEspNowMode()` and `releaseEspNowMode()` to `RadioManager` public API
- [ ] 1.3 Add `MqttManager*` field to `RadioManager`; inject via constructor or setter
- [ ] 1.4 Call `mqttManager->disconnectGracefully()` (blocking) inside `requestEspNowMode()` before switching radio
- [ ] 1.5 Call `mqttManager->reconnect()` inside `releaseEspNowMode()` after WiFi reconnects
- [ ] 1.6 Verify all existing game state tests and CLI tests pass after rename

## 2. State Preemption Interface

- [ ] 2.1 Add `virtual bool isPreemptable() const` to `State` base class with default `return true`
- [ ] 2.2 Override `isPreemptable()` to `return false` in QuickdrawDuelState (and any other timing-critical state) for the critical UX window
- [ ] 2.3 Add `virtual bool isMqttPreemptable() const` to `FdnGameState` base class with default `return true`
- [ ] 2.4 Add a fixed-capacity `kMcEvent` queue to the PDN device layer (initial capacity: 8)
- [ ] 2.5 Implement queue drain logic: on preemptable state transition, process in order, discard expired STALE_AND_DROP entries, always apply NEVER_DROP entries
- [ ] 2.6 Implement NEVER_DROP overflow displacement: evict oldest STALE_AND_DROP entry when queue full; log error if all entries are NEVER_DROP

## 3. MC Event Protocol

- [ ] 3.1 Add `kMcEvent` to `PktType` enum in `peer-comms-types.hpp`
- [ ] 3.2 Define `McEventPayload` struct with `McEventType type`, `DropPolicy dropPolicy`, `uint32_t ttlMs`, and event-specific data fields
- [ ] 3.3 Define `DropPolicy` enum class with `STALE_AND_DROP` and `NEVER_DROP` values
- [ ] 3.4 Register device-level `kMcEvent` handler in PDN `main.cpp` (or device bootstrap)
- [ ] 3.5 Implement handler: check `stateMachine->isPreemptable()`, apply immediately or enqueue with `receivedAtMs = millis()`

## 4. Topic Schema Constants

- [ ] 4.1 Add `MqttTopics` namespace (or equivalent constants) in `lib/core` with all downstream topic templates
- [ ] 4.2 Add upstream topic constants (`pdn/fdn/{fdnId}/status`, `pdn/fdn/{fdnId}/relay`)
- [ ] 4.3 Implement `fdnId` derivation from last 3 MAC octets at FDN boot

## 5. MQTT Driver Implementation

- [ ] 5.1 Choose and add MQTT client library dependency (e.g. PubSubClient or AsyncMqttClient) to `platformio.ini` FDN env
- [ ] 5.2 Implement `Esp32S3MQTTDriver::connect()` with `cleanSession = false`, MAC-derived `clientId`, and broker credentials from compile-time config
- [ ] 5.3 Implement `Esp32S3MQTTDriver::disconnect()` sending DISCONNECT packet
- [ ] 5.4 Implement `Esp32S3MQTTDriver::publish(topic, payload)` with QoS 1
- [ ] 5.5 Implement `Esp32S3MQTTDriver::subscribe(topic)` with QoS 1
- [ ] 5.6 Implement automatic reconnect with exponential backoff on unexpected disconnection
- [ ] 5.7 Wire `onMessage`, `onConnect`, `onDisconnect` callbacks

## 6. MqttManager Implementation

- [ ] 6.1 Implement `MqttManager` class with topic handler registration table
- [ ] 6.2 Implement `disconnectGracefully()` calling driver `disconnect()` and signalling completion
- [ ] 6.3 Implement `reconnect()` calling driver `connect()`
- [ ] 6.4 Implement incoming message dispatch with `isMqttPreemptable()` check and local queue for deferred messages
- [ ] 6.5 Implement downstream relay: on `pdn/broadcast/event` and `pdn/fdn/{fdnId}/pdns/render`, construct `kMcEvent` and broadcast via ESP-NOW
- [ ] 6.6 Implement upstream relay: expose `publishRelay(payload)` for FDN game states to call when relaying PDN events

## 7. FDN Integration

- [ ] 7.1 Instantiate `Esp32S3MQTTDriver` and `MqttManager` in FDN `main.cpp`
- [ ] 7.2 Subscribe FDN to `pdn/fdn/{fdnId}/#` and `pdn/broadcast/#` on `MqttManager` init
- [ ] 7.3 Register topic handlers for `config`, `app/launch`, `pdns/render` topics
- [ ] 7.4 Publish FDN status heartbeat to `pdn/fdn/{fdnId}/status` on connect and periodically
- [ ] 7.5 Inject `MqttManager` into `RadioManager`
- [ ] 7.6 Update `FdnGameState` base class `onGameMounted` to call `radioManager->requestEspNowMode()`
- [ ] 7.7 Update `FdnGameState` base class `onGameDismounted` to call `radioManager->releaseEspNowMode()`

## 8. PDN HTTP Client Removal

- [ ] 8.1 Remove WiFi credential compile-time constants from PDN build env in `platformio.ini`
- [ ] 8.2 Remove HTTP client code from PDN (`quickdraw-requests.hpp`, any `HttpRequest` queue usage in PDN)
- [ ] 8.3 Implement PDN-side upstream relay: PDN sends match results and player events as ESP-NOW packets to FDN instead of HTTP
- [ ] 8.4 Verify PDN firmware compiles without WiFi or HTTP dependencies

## 9. Home Assistant Configuration

- [ ] 9.1 Document Mosquitto add-on configuration requirements (persistent sessions enabled, QoS 1 support)
- [ ] 9.2 Document FDN broker credentials configuration (host, port, username, password as compile-time constants)
- [ ] 9.3 Document topic schema for HA automation authoring
