## Why

The server registration model (`DevicePlayer`) lived in `alleycat-server`, so app code that only needs the player profile had to depend on the HTTP/protobuf library, and PlayerFetch kept its own ad hoc session type. Moving the type into core and giving it one owner (`PlayerProvider`) lets registration UI, decode paths, and later gameplay migration share one concrete type.

## What Changes

- Move `DevicePlayer` from `lib/alleycat-server` into `lib/core/include/game/player-types.hpp` and rename it **`PlayerV2`**, alongside **`Team`**, **`Affiliation`**, and **`PlayerLimits`**. Fields and fixed-size storage are unchanged except team: `teamId` / `teamName` become `Team team`.
- `alleycat-server` includes `game/player-types.hpp`; `DevicePlayerDecoder` and `PlayerApi` produce `PlayerV2`. No `.proto` / nanopb regeneration.
- Add concrete **`PlayerProvider`** in core: optional `PlayerV2`, `hasPlayer` / `getPlayer` / `setPlayer` / `clear`; `getPlayer()` returns an empty default with `LOG_E` when nothing is stored.
- Remove `PlayerFetchSession`; `PlayerFetchApp` owns a `PlayerProvider` and states take `PlayerProvider*`. Awaken uses default Alleycat art.

Out of scope: unifying gameplay `Player` with `PlayerV2`; Quickdraw / Idle wiring; removing legacy JSON registration.

## Capabilities

### New Capabilities

- `player-types`: `PlayerV2`, `Team`, `Affiliation` in core.
- `player-provider`: Optional `PlayerV2` storage and access via `PlayerProvider`.

### Modified Capabilities

None (main-line `openspec/specs/` unchanged). PlayerFetch's in-flight `player-device-profile` capability is superseded at implementation time by `PlayerProvider`.

## Impact

- `lib/core/include/game/` — new `player-types.hpp`, `player-provider.hpp`; `lib/core/src/game/player-provider.cpp`.
- `lib/alleycat-server` — `device-player.hpp` deleted; decoder and `PlayerApi` use `PlayerV2`; `device_api.options` sizes now mirror `PlayerLimits`.
- `src/pdn/apps/player-fetch/` — session type removed; states use `PlayerProvider*`.
- `test/test_core` — decoder tests renamed to `PlayerV2`; new provider tests.
