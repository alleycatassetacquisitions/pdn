## ADDED Requirements

### Requirement: WirelessManager is renamed RadioManager
`WirelessManager` SHALL be renamed `RadioManager` across the entire codebase. All references in headers, source files, and instantiation sites SHALL be updated.

#### Scenario: Existing code compiles after rename
- **WHEN** the rename is applied
- **THEN** all existing game states and device code that previously referenced `WirelessManager` compile and behave identically using `RadioManager`

### Requirement: RadioManager coordinates graceful MQTT disconnect before entering ESP-NOW mode
When `RadioManager` switches to GAME_ACTIVE (ESP-NOW) mode, it SHALL call `MqttManager::disconnectGracefully()` and wait for completion before disabling WiFi.

#### Scenario: Game state requests ESP-NOW mode
- **WHEN** `requestEspNowMode()` is called on `RadioManager`
- **THEN** RadioManager calls `mqttManager->disconnectGracefully()`, waits for the DISCONNECT packet to be sent, then disables WiFi and enables ESP-NOW on ch 6

### Requirement: RadioManager reconnects MQTT after returning to WiFi mode
When `RadioManager` returns to ORCHESTRATION (WiFi) mode, it SHALL restore the WiFi connection and then call `MqttManager::reconnect()`.

#### Scenario: Game state releases ESP-NOW mode
- **WHEN** `releaseEspNowMode()` is called on `RadioManager`
- **THEN** RadioManager disables ESP-NOW, re-enables WiFi, and calls `mqttManager->reconnect()` once the WiFi connection is established

### Requirement: Game states interact with RadioManager only — no MQTT awareness required
Game states SHALL interact only with `RadioManager` via `requestEspNowMode()` and `releaseEspNowMode()`. No game state SHALL reference `MqttManager` directly.

#### Scenario: FdnGameState mounts
- **WHEN** `FdnGameState::onGameMounted()` is called
- **THEN** it calls `radioManager->requestEspNowMode()` and receives confirmation without any knowledge of MQTT
