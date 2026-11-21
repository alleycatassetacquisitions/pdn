# MQTT → FDN → PDN paging — agent handoff

**Date:** 2026-11-21  
**Repos:** `z:\CodingProjects\Alleycat\pdn` (firmware) and `z:\CodingProjects\Alleycat\MissionControl` (DNN / codecs)  
**Status:** Implemented and firmware builds clean (`esp32-s3_fdn_release`, `esp32-s3_pdn_release`). Venue / live broker end-to-end not proven on hardware in this session.

This document is the working brief for the next agent. Prefer this over the Cursor plan file.

---

## Goal

Mission Control pages players **without the PDN leaving ESP-Now**.

```
DNN (HA) --MQTT PageCmd--> Mosquitto --subscribe--> FDN
FDN shows page locally
FDN --ESP-Now kPageCommand (opaque JSON)--> nearby PDNs
PDN filters / dedupes / shows page from Idle
```

PDNs must **not** switch to WiFi for Mission Control messages. FDN is the only MQTT endpoint.

---

## Architecture decisions (doctrine-aligned — keep these)

| Decision | Why |
|---|---|
| **Opaque relay** | FDN forwards exact MQTT payload bytes inside `kPageCommand`. One schema (`PageCmd` JSON). Do **not** invent a second packed ESP-Now page struct. |
| **No RF ack this phase** | “Acknowledge” = display the page + remember `id`. No `kPageAck` until DNN has a consumer. |
| **DNN owns `page_id`** | `PageCmd.id` is a UUID assigned when DNN publishes. Never generate per-FDN ids. |
| **PDN owns filter** | Stamps `player_id` / `role` / `neocorp` filtered on the PDN. FDN does not drop pages because a player is not in `RemotePlayerManager`. |
| **Targeting via stamps, not MCS MAC** | MCS MAC lookup is still a stub. DNN already routes stamped pages on `mc/dnn/cmd/all/page`. |
| **Idle-safe interrupt only** | Page display from Idle (FDN Idle app / PDN Quickdraw Idle). No global app-return stack / duel interrupt yet. |
| **Page relay on channel 6** | PDNs pin `ESPNOW_CHANNEL` (6). FDN page relay fast-paths when the AP is already on 6; otherwise hops off WiFi, broadcasts on 6, then restores concurrent MQTT/STA. |

---

## Message contracts

### MQTT (FDN ← Mission Control)

Topics (AlleycatTV-style, `dnn` prefix):

| Topic | Role |
|---|---|
| `mc/dnn/cmd/device/{DEVICE_ID}/#` | Unicast to this FDN |
| `mc/dnn/cmd/all/#` | Every FDN |
| `mc/dnn/cmd/device/{DEVICE_ID}/membership` | BGC assign; retained |
| `mc/dnn/cmd/broadcast/{id}/#` | After membership (zone fan-out) |
| `mc/dnn/status/{DEVICE_ID}` | Retained `online` / empty LWT |

`DEVICE_ID` = STA MAC as lowercase hex with no separators (see `mac-device-id.hpp`).

**Action handled:** `page` only (JSON). LED/haptic ignored for now.

**PageCmd JSON** (SoR: Mission Control `dnn_commands.proto`):

```json
{
  "text": "Hello",
  "duration": 10,
  "scroll": false,
  "player_id": "",
  "role": "",
  "neocorp": "",
  "id": "<uuid from DNN>"
}
```

Empty stamps = broadcast to all PDNs. Non-empty stamps = PDN local filter.

### ESP-Now (FDN → PDN)

- `PktType::kPageCommand = 17` in `peer-comms-types.hpp`
- Wire: `DataPktHdr` + **exact** MQTT UTF-8 JSON bytes
- Broadcast MAC for nearby PDNs
- Max opaque payload ~1400 bytes (`PageManager::MAX_OPAQUE_PAYLOAD`); oversized rejected with log (no fragmentation yet)

---

## What was implemented

### Mission Control (`MissionControl`)

| File | Change |
|---|---|
| `Apps/Data_Node_Nexus/docs/dnn_commands.proto` | `PageCmd.id = 7` |
| `Libraries/Shared_HA_Helpers/.../mc_codec.py` | field 7 `id` |
| `Apps/AlleycatTV/.../mc_codec.py` | same (keep in sync) |
| `Apps/Data_Node_Nexus/.../payloads.py` | `encode_page_cmd(..., id=)` |
| `Apps/Data_Node_Nexus/.../__init__.py` | `id=str(uuid.uuid4())` on page service |
| `Apps/Data_Node_Nexus/HA_Component/tests/test_payloads.py` | asserts `id` |

### PDN firmware shared (`pdn/lib/core`)

| File | Role |
|---|---|
| `include/wireless/page-cmd.hpp` | Parse JSON; `pageCmdMatchesPlayer` |
| `include/wireless/page-manager.hpp` | Dedupe ring, pending page, FDN `ingestMqttPayload`, PDN `processPacket` |
| `include/wireless/page-display.hpp` | OLED + duration timer + primary dismiss |
| `include/device/wireless-manager.hpp` | `WIFI_AND_ESPNOW`, concurrent bring-up, `sendEspNowPageRelay()` (ch6 fast path / hop+restore) |
| `include/device/drivers/peer-comms-types.hpp` | `kPageCommand` |
| `include/device/drivers/mqtt-interface.hpp` / `mqtt-types.hpp` | Reworked usable MQTT API |

### ESP32 drivers

| File | Role |
|---|---|
| `lib/esp32-drivers/.../esp32-s3-mqtt-driver.hpp` | ESP-IDF `esp_mqtt_client`; queue inbound; LWT; `onConnected` hook |
| `lib/esp32-drivers/.../esp-now-driver.hpp` | If STA already associated, **keep AP** (no disconnect / no forced channel pin) |
| `lib/esp32-drivers/.../crash-logger.hpp` | Do not collapse `WIFI_AND_ESPNOW` back to ESP-NOW-only on upload restore |

### FDN (`pdn/src/fdn`)

| File | Role |
|---|---|
| `main.cpp` | Concurrent radio bring-up → MQTT → Idle launch; page manager wired |
| `wireless/dnn-mqtt-client.hpp` | Subscribe device/all/membership/broadcast; page → `PageManager::ingestMqttPayload` |
| `wireless/mac-device-id.hpp` | Fabric device id from MAC |
| `apps/idle/*` | `PAGE_DISPLAY` state; Idle transitions when pending page |

### PDN (`pdn/src/pdn`)

| File | Role |
|---|---|
| `main.cpp` | `PageManager` + `kPageCommand` handler |
| `game/quickdraw.{hpp,cpp}` | Takes `PageManager*`; Idle ↔ PageDisplay |
| `game/quickdraw-states.hpp` | `PAGE_DISPLAY = 30`; `PageDisplay` state |
| `game/quickdraw-states/page-display-state.cpp` | Display controller |
| `game/quickdraw-states/idle-state.cpp` | `transitionToPageDisplay()` |

### Build / credentials

| File | Change |
|---|---|
| `platformio.ini` | `[mqtt]` defaults; FDN envs get `MQTT_*` build flags |
| `wifi_credentials.ini.example` | `[mqtt]` section |
| `src/CMakeLists.txt` | `REQUIRES mqtt` (built-in IDF 5.5 component — **do not** add `espressif/esp-mqtt` for this platform pin) |
| `test/test_core/page-manager-tests.hpp` | Parse / filter / dedupe / relay unit tests |

---

## Radio / venue constraints (load-bearing)

1. **Paging does not require the venue AP on channel 6.** `WirelessManager::sendEspNowPageRelay` sends on `ESPNOW_CHANNEL` (6) via a fast path when already there, or a brief WiFi→ch6→WiFi hop otherwise (MQTT drops and resubscribes during the hop).
2. Other FDN↔PDN ESP-Now (nearby players, FDN connect, symbol-match) still prefer a venue AP on channel 6 on single-radio boards; dual-radio hardware is the long-term fix for full RF freedom.
3. FDN bring-up order in `fdn/main.cpp`:
   - Crash upload / restore first (exclusive WiFi if needed)
   - Then `beginConcurrentWifi()` → wait for IP → `attachConcurrentEspNow()`
   - Then MQTT start
4. Ethernet MQTT remains out of scope for now (dual-radio is the preferred path to permanent channel decoupling).

---

## How to prove on hardware

### Prerequisites

1. Copy/fill `wifi_credentials.ini` from example (`[wifi]` + `[mqtt]` + `[fdn]`).
2. Mosquitto on HA reachable at `MQTT_BROKER:MQTT_PORT` with those creds (same as AlleycatTV).
3. Venue AP may be any 2.4 GHz channel for **paging**; channel 6 still helps other FDN↔PDN ESP-Now.
4. Flash FDN (`esp32-s3_fdn_release`) and PDN (`esp32-s3_pdn_release`).
5. Deploy updated DNN / `mc_codec` to HA so pages include `id`.

### Scenario 1 — all PDNs

- DNN page with `target: all` (no stamps).
- Expect: every online FDN shows text; nearby PDNs in Idle show once; walking between FDNs does not re-show same `id`.

### Scenario 2 — stamped player / role / neocorp

- DNN page with `player_id` and/or `role` (`hunter`|`bounty`) and/or `neocorp` (`helix`|`endline`|`reboot`|`freelancer`…).
- Expect: only matching PDNs show; FDN still shows locally and still relays.

### Scenario 3 — zone (broadcast group)

- BGC assigns FDN membership → FDN subscribes `mc/dnn/cmd/broadcast/{id}/#`.
- Page that group; only those FDNs fan out.

### Serial checks

- FDN: `DnnMqtt` subscribe / `PageManager` pending / `WirelessManager` page relay fast path or hop+restore / `ESP-Now page relay ok`.
- FDN appears in HA via `mc/dnn/status/{mac}`.
- PDN: duplicate skip / filter skip / Idle → page display.

### Channel hop prove-out

1. Leave UniFi 2.4 GHz on a **non-6** channel; page with target all → expect hop logs and PDN display.
2. Pin AP to channel 6; page again → expect fast-path log (no hop / no MQTT blip).

---

## Explicit non-goals (do not expand unless asked)

- `kPageAck` / DNN delivery UI
- LED / haptic MQTT actions
- MQTT protobuf wire flip (`MC\x01`)
- App-stack interrupt during duels
- ESP-Now fragmentation / unicast optimization
- PDN WiFi for Mission Control
- MCS `lookup_by_mac`
- Ethernet MQTT path

---

## Likely next work for another agent

1. **Live venue e2e** with real DNN panel + FDN + PDN on a non-6 AP; confirm hop restore + MQTT resubscribe.
2. **Deploy MC changes** to the running HA instance if not already deployed.
3. **Native tests:** `page-manager-tests.hpp` exists; this Windows host lacked `g++` on PATH for `pio test -e native` — run where the native toolchain works.
4. If Idle is not the active FDN app in some flows, ensure page pending is drained (currently FDN launches Idle for prove-out).
5. **Dual-radio FDN board:** WiFi ESP for MQTT on any venue channel + ESP-Now ESP pinned to 6 (removes hop and fixes non-page RF).
6. Optional later: duel-safe interrupt, cloud ack, ethernet, protobuf dual-read.

---

## Reference docs (Mission Control)

- `MissionControl/Docs/MQTT Communication Principles.md`
- `MissionControl/Docs/Design Terms.md` (FDN page state machine; DNN owns paging)
- `MissionControl/Apps/Data_Node_Nexus/docs/dnn_commands.proto`
- Proven client pattern: `MissionControl/Apps/AlleycatTV/Client_Component/alleycattv_player/mqtt_handler.py`

---

## Quick file map for navigation

```
pdn/
  lib/core/include/wireless/page-{cmd,manager,display}.hpp
  lib/core/include/device/wireless-manager.hpp
  lib/esp32-drivers/.../esp32-s3-mqtt-driver.hpp
  lib/esp32-drivers/.../esp-now-driver.hpp          # concurrent STA keep-AP
  src/fdn/main.cpp
  src/fdn/wireless/dnn-mqtt-client.hpp
  src/fdn/apps/idle/...
  src/pdn/main.cpp
  src/pdn/game/quickdraw*.{hpp,cpp}
  test/test_core/page-manager-tests.hpp
  docs/MQTT_FDN_PDN_PAGING_HANDOFF.md               # this file

MissionControl/
  Apps/Data_Node_Nexus/... (PageCmd.id + UUID publish)
  Libraries/Shared_HA_Helpers/.../mc_codec.py
```
