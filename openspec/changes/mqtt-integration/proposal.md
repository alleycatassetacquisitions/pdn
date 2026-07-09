## Why

The system needs a Mission Control layer (Home Assistant) that can orchestrate FDN and PDN devices across a venue — pushing story events, easter eggs, configuration changes, and game orchestration — without any device needing a direct connection to the Rust server. The current HTTP-only architecture requires PDNs to carry WiFi credentials and make independent API calls, which couples them to server availability and prevents any centralized, real-time push to players.

## What Changes

- **FDN gains an MQTT client** connected to a Home Assistant-hosted broker, replacing direct HTTP calls from PDNs to the Rust server
- **RadioManager** (renamed from `WirelessManager`) coordinates two FDN operating modes: ORCHESTRATION (WiFi + MQTT active) and GAME_ACTIVE (ESP-NOW ch 6, MQTT disconnected with persistent session)
- **FDN acts as a two-way relay**: subscribes to MC commands and publishes PDN-originated events upstream via MQTT
- **New `kMcEvent` ESP-NOW packet type** is registered at the PDN device level (not per-state) for receiving MC-originated broadcasts from FDN
- **Preemption interface** added to the `State` base class, allowing critical states (e.g. quickdraw duel) to defer incoming global events until a safe transition point
- **PDN HTTP client removed** — all PDN communication flows through FDN
- **BREAKING**: PDN no longer connects to WiFi or makes any HTTP requests

## Capabilities

### New Capabilities

- `mqtt-driver`: `Esp32S3MQTTDriver` implementing `MQTTInterface` — connect/disconnect, publish, subscribe, QoS 1, persistent session, reconnect policy
- `mqtt-manager`: FDN-side `MqttManager` above the driver — topic subscription table, message dispatch, upstream relay (PDN events → MQTT publish), downstream relay (MC messages → ESP-NOW broadcast)
- `radio-manager`: Renamed and extended `WirelessManager` — owns ORCHESTRATION/GAME_ACTIVE mode transitions, coordinates graceful MQTT disconnect before entering ESP-NOW mode
- `mc-event-protocol`: `kMcEvent` ESP-NOW packet type and `McEventPayload` — carries MC-originated events to PDNs with `DropPolicy` (STALE_AND_DROP | NEVER_DROP) and `ttlMs`
- `state-preemption`: `isPreemptable()` virtual method on `State` base class — PDN device-level handler checks before applying queued `kMcEvent` packets; queue drains on next preemptable transition with TTL-based stale drop
- `mqtt-topic-schema`: Topic hierarchy constants for FDN subscribe/publish — `pdn/fdn/{fdnId}/config`, `pdn/fdn/{fdnId}/app/launch`, `pdn/broadcast/event`, `pdn/fdn/{fdnId}/status`, `pdn/fdn/{fdnId}/relay`

### Modified Capabilities

- `wireless-manager`: Renamed to `RadioManager`; gains ORCHESTRATION/GAME_ACTIVE state, MQTT coordination handshake, removes HTTP burst mode-switch pattern

## Impact

- **FDN firmware** (`src/fdn/`): gains MQTT driver, MqttManager, RadioManager update
- **PDN firmware** (`src/pdn/`): HTTP client removed, `kMcEvent` handler added at device level, `State` base updated
- **`lib/core`**: `State` base gains `isPreemptable()`, new `McEventPayload` types, topic schema constants
- **`lib/esp32-drivers`**: `Esp32S3MQTTDriver` implemented (currently stubbed)
- **Home Assistant**: MQTT broker (Mosquitto add-on) must be configured with persistent sessions enabled
- **Rust server**: No changes — server interaction goes through HA, not devices directly
