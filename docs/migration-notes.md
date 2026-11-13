# PDN/FDN Monorepo Migration Notes

## Functionality / Logic Changes

These change runtime behavior or API contracts.

---

### State machine redesign (`StateLifecycle` / `State` / `TypedState<DeviceT>`)

- Introduced `StateLifecycle` base with pure-virtual `mount` / `loop` / `dismount` / `pause` / `resume`
- `State` bridges these to the `onStateMounted(Device*)` / `onStateLoop` / `onStateDismounted` user API via `private` overrides — implementers cannot accidentally call or override the bridge methods
- `TypedState<DeviceT>` eliminates all manual `static_cast` in concrete states; the device pointer arrives pre-cast as `DeviceT*` to every lifecycle method

---

### Generic peripheral accessor on `Device`

- Added a single `getPeripheral<T>()` template method; the per-type virtual getters (`getDisplay()`, `getPrimaryButton()`, etc.) stay on `Device` as wrappers over it
- Added `virtual void* abstractSelf() = 0` to `DriverInterface` and implemented it in each combined interface (`DisplayDriverInterface`, etc.) to enable RTTI-less cross-casting under `-fno-rtti`

---

### `ShootoutManager` and `MatchManager` wiring

- `Idle` reads `ShootoutManager` from the `GameContext` it is built from, like every other state
- `MatchManager` holds its own pointer for the role-mismatch gate and exposes no getter. `ShootoutManager` sets it in its constructor and clears it in its destructor

---

### `LightManager` decoupled from concrete animation types

- `LightManager` previously imported and hardcoded specific animation classes
- Refactored to accept an `AnimationBase*` — animations are now injectable

---

### `WS2812BLedStripDriver` (historical FastLED note)

- `WS2812BFastLEDDriver` took its pins as template parameters because FastLED's `addLeds<>()` needs compile-time constant pins
- Later replaced by `WS2812BLedStripDriver`, which takes them as runtime constructor arguments
- `numDisplayLights` and `numGripLights` remain runtime constructor parameters

---

### Driver constructors made configurable

- `SSD1306U8G2Driver`: `csPin`, `dcPin`, `rstPin` now constructor parameters
- `Esp32s3SerialOut` / `Esp32s3SerialIn`: `txPin`, `rxPin` now constructor parameters
- All pins pass through from `pdn-constants.hpp` at instantiation in `main.cpp` — no more hardcoded values inside driver implementations

---

### `wireless-manager.hpp` log strings

- `enablePeerCommsMode()` log strings no longer reference `ESPNOW_CHANNEL` (which is ESP32-only and unavailable in native builds)
- "Switching to ESP-NOW mode on channel %d..." → "Switching to ESP-NOW mode..."
- "ESP-NOW mode enabled on channel %d" → "ESP-NOW mode enabled"

---

### `device-constants.hpp` eliminated — constants redistributed

| Constant group | New home |
|---|---|
| Hardware pin constants | `src/pdn/pdn-constants.hpp` |
| Protocol / comms constants | `lib/core/include/protocol-constants.hpp` |
| ESP32-specific (`ESPNOW_CHANNEL`, baud rates, GPIO) | `lib/esp32-drivers/include/esp32-driver-constants.hpp` |
| Game string resources (`alleycatImageCollection`, etc.) | `src/pdn/game/quickdraw-resources.hpp` as `inline const` |
| Match prefs keys (`PREF_SEQ_KEY`, etc.) | `inline constexpr` in `src/pdn/game/match-manager.hpp` |
| NVS namespace | `MATCHES_PREFS_NAMESPACE` in `src/pdn/game/match-manager.hpp` |

---

### Easing curves extracted to utility header

- Moved from inline in `LightManager` to `lib/core/include/utils/easing-curves.hpp`

---

## Compilation Fixes

Not behavior changes — fix build errors that were latent or newly exposed by the migration.

- `http-client-interface.hpp`: Added `#include <cstdint>` — `uint8_t` was unavailable in native (non-Arduino) builds
- `quickdraw-resources.hpp`: Changed 17 global `const` array/string declarations to `inline const` — fixes ODR violations when the header is included in multiple translation units
- Various `#include` paths updated after file moves (e.g. bare `"device.hpp"` → `"device/device.hpp"`)

---

## File Structure Migration

### `lib/core/` — new shared cross-platform library

- All device interfaces, state machine framework, wireless managers, and utilities
- `include/device/`, `include/state/`, `include/wireless/`, `include/utils/` moved here
- Corresponding `.cpp` files moved to `lib/core/src/`

### `lib/esp32-drivers/` — new ESP32-only library

- `src/device/drivers/esp32-s3/` → `lib/esp32-drivers/include/device/drivers/esp32-s3/`
- New `esp32-driver-constants.hpp` with hardware-specific constants
- `library.json` restricts compilation to `espressif32` platform

### `lib/native-drivers/` — new native simulation library

- `src/device/drivers/native/` → `lib/native-drivers/`
- `library.json` restricts compilation to `native` platform

### `src/pdn/` — PDN-specific code

| Old path | New path |
|---|---|
| `src/device/pdn.*` | `src/pdn/device/` |
| `src/game/` | `src/pdn/game/` |
| `src/apps/` | `src/pdn/apps/` |
| `src/wireless/` | `src/pdn/wireless/` |
| `src/symbol-match/` | `src/pdn/symbol-match/` (removed in #326; FDN counterpart lives in `src/fdn/apps/symbol-match/`) |
| `src/main.cpp` | `src/pdn/main.cpp` |
| _(new)_ | `src/pdn/pdn-constants.hpp` |

### `src/cli/` — CLI / simulator code

| Old path | New path |
|---|---|
| `src/cli-main.cpp` | `src/cli/cli-main.cpp` |
| `src/perf-main.cpp` | `src/cli/perf-main.cpp` |
| `include/cli/*.hpp` | `src/cli/` |

### Deleted

- `include/device/device-constants.hpp`
- `include/cli/` (emptied by move)
- `include/device/` (emptied by move)
- `include/` (completely empty after moves, no longer needed)

---

## Build System Changes

### `platformio.ini`

- ESP32 environment renames: `esp32-s3_release/debug` → `esp32-s3_pdn_release/debug`
- `esp32-s3_base`: added `-I src/pdn`; `build_src_filter` simplified to `-<cli/*>` (later replaced by `framework = espidf` with `board_build.cmake_extra_args = -DPDN_DEVICE=pdn` in #284)
- All native envs: build flags updated to `-I src/pdn -I src`; `build_src_filter` updated to use new `pdn/main.cpp` and `cli/*` paths
- `googletest` removed from `lib_deps` in test envs — `test_framework = googletest` already owns it (having both caused PlatformIO to cycle/remove it during the build)

### CI / scripts / docs

Files updated to replace all `esp32-s3_release` / `esp32-s3_debug` references with `esp32-s3_pdn_release` / `esp32-s3_pdn_debug`:

- `.github/workflows/pr-checks.yml`
- `scripts/flash_multi.py`
- `scripts/multi_flash_target.py`
- `README.md`

---

## Whitespace / Line Ending Changes

- Added `.gitattributes` enforcing `* text=auto eol=lf` across the repo
- Ran `git add --renormalize .` to rewrite all tracked files to LF
- No content changes — only line ending bytes
