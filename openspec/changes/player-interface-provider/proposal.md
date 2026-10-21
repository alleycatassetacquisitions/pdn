## Why

PlayerFetch and `DevicePlayer` already hold the server registration model, but app code still reaches for ad hoc session types and legacy `Player` fields. We need a **core-owned read contract** (`PlayerInterface`) and **session ownership** (`PlayerProvider`) so registration UI, decode paths, and future gameplay migration share one vocabulary without coupling to JSON legacy or protobuf wire types.

## What Changes

- Add `lib/core/include/game/player-types.hpp` (and matching `.cpp` if needed) with **`Team`**, **`Affiliation`**, and abstract **`PlayerInterface`** (player id, name, team, affiliations, active — no wire metadata).
- Add **`PlayerProvider`** in core: holds an optional profile, exposes `const PlayerInterface&` (stored profile or a default object with `LOG_E` when absent), supports set/clear.
- Make **`DevicePlayer`** in `lib/alleycat-server` **publicly implement `PlayerInterface`**, mapping team and affiliation fields without changing protobuf codegen or decoder wire mapping.
- Follow-up wiring (not required to land the core contract): migrate `PlayerFetchSession` to implement `PlayerProvider`, simplify awaken to default allegiance art, and point states at `PlayerProvider*` where applicable.

Out of scope for this change: unifying gameplay `Player` stats/RNG with `PlayerInterface`; Quickdraw / Idle wiring; removing legacy JSON registration; retaining event metadata in session storage.

## Capabilities

### New Capabilities

- `player-types`: Registration value types (`Team`, `Affiliation`) and `PlayerInterface` read API in core.
- `player-provider`: Optional profile storage and access via `PlayerProvider`.

### Modified Capabilities

None (main-line `openspec/specs/` unchanged). PlayerFetch’s in-flight `player-device-profile` capability is superseded at implementation time by `PlayerProvider`, not by editing the archived player-fetch change.

## Impact

- `lib/core/include/game/` — new `player-types.hpp`, `player-provider.hpp`; possible small helper `.cpp` for provider default behavior.
- `lib/core` tests — interface conformance for `DevicePlayer`, provider set/get/clear/default `getPlayer`.
- `lib/alleycat-server/include/alleycat-server/device-player.hpp` — inheritance + override implementations; **no** `.proto` / nanopb regeneration.
- Optional later: `src/pdn/apps/player-fetch/player-fetch-session.hpp`, awaken simplification, and states.
