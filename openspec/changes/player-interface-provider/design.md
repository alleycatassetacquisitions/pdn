## Context

- **Wire:** `DevicePlayerResponse` decodes into registration fields on `Player` (`device-player-decoder.cpp`). Nanopb types live only in `device_api.pb.h`.
- **Session:** `PlayerProvider` owns one `Player` for the device session; `main` passes `playerProvider.getPlayer()` to Quickdraw and `&playerProvider` to PlayerFetch.
- **Dependency:** `alleycat-server` depends on `core` and decodes into `Player`.

## Goals / Non-Goals

**Goals:**

- Registration payload (id, name, team, affiliations) on `Player` with dedicated accessors for PlayerFetch.
- **`PlayerProvider`** owns the session `Player` and registration PIN; optional profile via `Player::hasRegistrationProfile()`.
- Private legacy shim on `Player` after registration apply so Quickdraw call sites stay unchanged.
- Zero protobuf/schema changes.

**Non-Goals:**

- An abstract player interface.
- Affiliation-driven awaken art in PlayerFetch.

## Decisions

### `player-types.hpp` holds shared value types

`PlayerLimits`, `Team`, and `Affiliation` remain in `game/player-types.hpp`. The full registration record lives on `Player`.

### Codec stays in alleycat-server

`DevicePlayerDecoder::decode(body, Player&)` fills registration fields and calls `Player::applyRegistrationProfile` (or equivalent) so legacy fields sync.

### `PlayerProvider`

```text
getPlayer() -> Player*          // always non-null; owned member
hasPlayer() -> bool             // getPlayer()->hasRegistrationProfile()
clear()                         // registration profile + PIN
setRegistrationPin / getRegistrationPin / applyRngSeedFromRegistrationPin
```

### PlayerFetch wiring

States take `PlayerProvider*`. PIN and clear go through the provider; registration reads/writes go through `getPlayer()` using server field accessors only.

## Migration Plan

1. Move registration fields onto `Player`; decoder targets `Player`.
2. Repurpose `PlayerProvider` to own `Player`.
3. Wire `main` with a single provider instance.
