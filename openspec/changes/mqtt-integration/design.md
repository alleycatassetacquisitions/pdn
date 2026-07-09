## Context

The PDN/FDN ecosystem currently uses HTTP REST for device-to-server communication and ESP-NOW for peer-to-peer device communication. HTTP calls are burst-mode: the FDN's `WirelessManager` (soon `RadioManager`) switches the single 2.4GHz radio to WiFi, fires the request, then switches back to ESP-NOW channel 6. There is no persistent connection and no server-to-device push capability.

Mission Control is a Home Assistant instance that will sit alongside the Rust server. MC needs to push orchestration events (story moments, easter eggs, game configuration, FDN activation) to devices at runtime. The Rust server continues to own player and match data; MC queries it via REST.

**Constraints:**
- ESP32-S3 has one 2.4GHz radio — WiFi and ESP-NOW cannot run on different channels simultaneously
- ESP-NOW is hardcoded to channel 6 at compile time; all devices rely on this constant
- MQTT requires a persistent TCP connection over WiFi
- PDNs do not connect to WiFi in the new architecture
- C++17 embedded target — no RTTI, limited heap
- Existing `ConnectState<PDN>` / `ConnectState<FDN>` lifecycle must be preserved

## Goals / Non-Goals

**Goals:**
- FDN maintains a persistent MQTT connection to HA broker during lobby/idle periods
- FDN broadcasts MC-originated events to all nearby PDNs over ESP-NOW ch 6
- FDN relays PDN-originated events upstream to MC via MQTT publish
- PDN never has any knowledge of MQTT or the server layer
- Critical game states on both PDN and FDN are protected from mid-event interruption
- MC-pushed events that cannot be delivered immediately are queued with configurable TTL

**Non-Goals:**
- Simultaneous WiFi + ESP-NOW on different channels (requires uncontrollable venue infrastructure)
- Real-time MQTT delivery during active game states (MQTT is async orchestration, not game comms)
- Per-PDN MQTT targeting (FDN relays to all nearby PDNs via broadcast)
- HA automation logic or Rust server API changes
- PDN-to-FDN HTTP relay (all upstream data goes ESP-NOW → FDN → MQTT)

## Decisions

### 1. Mode-switching preserved: ORCHESTRATION ↔ GAME_ACTIVE

**Decision:** The FDN continues to mode-switch its radio. ORCHESTRATION mode: WiFi on, MQTT connected, ESP-NOW off. GAME_ACTIVE mode: WiFi off, MQTT cleanly disconnected, ESP-NOW ch 6 active.

**Rationale:** The alternative — simultaneous WiFi + ESP-NOW — requires both to run on the same channel. This channel is dictated by the venue WiFi AP and cannot be guaranteed to match ch 6. Fragmenting ESP-NOW across channels breaks the all-PDN broadcast model. Mode-switching is proven in the existing codebase.

**MQTT compensates with persistent sessions:** When FDN disconnects MQTT before entering GAME_ACTIVE, the broker retains all QoS 1 messages. On reconnect, the broker drains the queue atomically. No custom replay mechanism is needed — this is native MQTT behaviour.

**Timing:** MQTT handles out-of-band orchestration (lobby, between rounds). ESP-NOW ch 6 handles in-game real-time events. These concerns have naturally different timing requirements; the mode boundary aligns with them.

### 2. RadioManager owns the mode transition and MQTT coordination

**Decision:** `WirelessManager` is renamed `RadioManager`. When a game state requests ESP-NOW mode, `RadioManager` calls `mqttManager->disconnectGracefully()` before completing the switch. When returning to WiFi mode, `RadioManager` calls `mqttManager->reconnect()`. Game states interact only with `RadioManager` — same seam as today.

**Rationale:** Game states must not know about MQTT. `RadioManager` is the single owner of the radio and is the correct coordinator. Graceful disconnect (sending MQTT DISCONNECT packet) preserves the persistent session on the broker; an ungraceful drop could trigger the Last Will message and degrade session quality.

**Alternative considered:** MqttManager owns WiFi and game states ask it to yield. Rejected — this inverts the dependency; game states would need to know a WiFi owner exists.

### 3. kMcEvent is a new ESP-NOW packet type, registered at device level

**Decision:** MC-originated events that need to reach PDNs are delivered as a new `kMcEvent` ESP-NOW packet type. The handler is registered once at PDN device initialisation (not per-state). FDN broadcasts `kMcEvent` to all nearby PDNs over ch 6 (not just jack-connected PDNs).

**Rationale:** These events are global by nature — push notifications, story events, easter eggs — and are not bound to any app or game state. Registering per-state would require every state to handle them. Device-level registration keeps states clean and ensures no state can accidentally miss registration.

### 4. isPreemptable() on State base class, checked by device-level handler

**Decision:** The `State` base class gains `virtual bool isPreemptable() const` defaulting to `true`. The PDN device-level `kMcEvent` handler checks the current state's preemptability before applying. If not preemptable, the event is queued. On the next preemptable transition, the queue is drained with TTL-based stale drop.

**Rationale:** Critical states (quickdraw countdown, win/lose reveal) must not be visually interrupted. The base-class default means only states that need protection override to `false` — minimal code change for the common case.

**FDN equivalent:** `FdnGameState` base class exposes `isMqttPreemptable()` for the same purpose. The `MqttManager` checks this before applying incoming MQTT messages.

### 5. Drop policy specified at publish time, TTL measured from PDN receipt

**Decision:** `McEventPayload` carries `DropPolicy dropPolicy` (STALE_AND_DROP | NEVER_DROP) and `uint32_t ttlMs`. TTL is measured from when PDN receives the ESP-NOW packet — no clock synchronisation required. On queue drain, stale events are discarded, NEVER_DROP events are always applied.

**Rationale:** MC knows which events are time-sensitive (easter egg window) vs. critical (game closing announcement). Measuring from PDN receipt avoids clock sync across MC, FDN, and PDN. NEVER_DROP events are bounded only by queue capacity.

**Queue overflow:** When the queue is full, new STALE_AND_DROP events are rejected. NEVER_DROP events displace the oldest STALE_AND_DROP entry if capacity is reached.

### 6. FDN acts as full two-way MQTT relay

**Decision:** FDN subscribes to MC topics (downstream) and publishes PDN-originated events upstream (e.g. match results, player registration requests). HA receives FDN publishes and calls the Rust server REST API on behalf of the FDN. FDN does not make direct HTTP calls to the Rust server.

**Rationale:** Gives FDN a single network connection (MQTT) rather than two (MQTT + HTTP). HA becomes the integration hub for all server communication. PDN HTTP client is removed entirely — all upstream data flows ESP-NOW → FDN → MQTT.

### 7. Topic schema uses fdnId derived from device MAC

**Decision:** FDN-specific topics use `pdn/fdn/{fdnId}/...` where `fdnId` is the last 3 octets of the FDN MAC address (e.g. `pdn/fdn/a1b2c3/config`). Global broadcast uses `pdn/broadcast/event`. FDN subscribes to both its specific topic subtree and the broadcast topic.

**Rationale:** MAC-derived IDs are unique, require no provisioning step, and are already used in the ESP-NOW peer table. Short suffix (3 octets) keeps topic strings readable in HA dashboards.

## Risks / Trade-offs

- **MQTT messages delayed until next ORCHESTRATION window** → Mitigation: MQTT is scoped to orchestration content (story, config, easter eggs) which tolerates delay. Real-time game events remain on ESP-NOW. MC should not use MQTT for time-critical in-game commands.

- **Graceful MQTT disconnect adds latency to game start** → Mitigation: DISCONNECT packet is a single TCP frame; typically completes in <10ms on a local network. Acceptable for state transition timing.

- **NEVER_DROP queue overflow under prolonged non-preemptable state** → Mitigation: Define a reasonable max queue depth (e.g. 8 events). NEVER_DROP events with identical content could be deduplicated by type.

- **ESP-NOW ch 6 broadcast range vs. PDN distance** → No mitigation needed; this is an existing deployment concern unchanged by this feature.

- **FDN crash or power loss during GAME_ACTIVE discards queued MC events** → Mitigation: QoS 1 persistent session means broker re-delivers on reconnect; PDN-side queue is transient by design.

## Migration Plan

1. Rename `WirelessManager` → `RadioManager` across the codebase
2. Implement `Esp32S3MQTTDriver` (existing stubs in `lib/esp32-drivers`)
3. Implement `MqttManager` on FDN with topic table, dispatch, graceful disconnect/reconnect API
4. Update `RadioManager` to call `mqttManager` on mode transitions
5. Add `kMcEvent` to `peer-comms-types.hpp`; define `McEventPayload` with `DropPolicy` and `ttlMs`
6. Register device-level `kMcEvent` handler in PDN `main.cpp`
7. Add `isPreemptable()` to `State` base class; override in quickdraw critical states
8. Implement PDN event queue with TTL drain logic
9. Remove PDN WiFi credentials and HTTP client
10. Add `isMqttPreemptable()` to `FdnGameState` base class
11. Configure HA Mosquitto add-on with persistent sessions enabled

## Open Questions

1. **Max event queue depth** — what is the right cap for the PDN-side `kMcEvent` queue? Starting point: 8 events.

2. **MQTT keep-alive interval** — 60 seconds is a reasonable default; needs tuning against venue WiFi AP idle timeout settings.

3. **FDN identity provisioning** — MAC-derived `fdnId` works but requires HA to know which FDN maps to which physical location. Is there a provisioning step, or does this come from a static mapping in HA config?

4. **Mid-state MQTT updates on FDN** — can `FdnGameState` concrete states receive and apply MQTT config updates mid-state? Needs team discussion (mirrors open question in controller architecture design).
