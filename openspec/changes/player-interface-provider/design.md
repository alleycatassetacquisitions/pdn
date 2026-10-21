## Context

- **Wire:** `DevicePlayerResponse` decodes into a hand-written struct (`device-player-decoder.cpp`). Nanopb types live only in `device_api.pb.h`.
- **Before:** `DevicePlayer` lived in `alleycat-server`; `PlayerFetchSession` stored one by value; awaken walked affiliations for art.
- **Gameplay:** `game/player.hpp` `Player` is a separate concrete class (PIN, stats, legacy allegiance enum, hunter). It is not the registration profile.
- **Dependency:** `alleycat-server` already depends on `core` (`library.json`), so it can include a core type without inverting layers.

## Goals / Non-Goals

**Goals:**

- One concrete registration type, **`PlayerV2`**, in core.
- **`PlayerProvider`** as the single session owner (optional profile, set/clear, const access).
- Zero protobuf/schema changes.

**Non-Goals:**

- An abstract player interface. One concrete type is enough until a second implementation exists.
- Replacing or merging gameplay `Player` with `PlayerV2` (deprecation comment on `Player` only).
- Affiliation-driven awaken art in PlayerFetch.

## Decisions

### `player-types.hpp` owns the registration model

`lib/core/include/game/player-types.hpp` holds `PlayerLimits`, `Team`, `Affiliation`, and `PlayerV2`. No Arduino/HTTP includes, so firmware and native tests can include it.

`PlayerV2` is the former `DevicePlayer`, renamed to avoid colliding with gameplay `Player` while signalling it is the successor. Storage follows the `protobuf-types` memory model: ids are `UUIDBytes`, strings are fixed `char[]` sized by `PlayerLimits`. Team is a `Team team` member; `hasTeam()` checks `team.id` against nil.

*Alternative:* abstract `PlayerInterface` implemented by `DevicePlayer`. Rejected — one implementation, and the interface forced field copies and getters with no consumer benefit.

### Codec stays in alleycat-server

`DevicePlayerDecoder` (named for the wire message) decodes into `PlayerV2`. `static_assert`s in the decoder tie `PlayerLimits` to `device_api.options` sizes.

### `PlayerProvider` is concrete

```text
hasPlayer() const -> bool
getPlayer() const -> const PlayerV2&   // stored profile, or empty default + LOG_E when none
setPlayer(const PlayerV2& profile)     // stores a full copy
clear()
```

When `!hasPlayer()`, `getPlayer()` returns the empty member `PlayerV2`, logs `LOG_E`, and does not assert — stopgap until all call sites guard with `hasPlayer()`.

### PlayerFetch wiring

`PlayerFetchApp` owns a `PlayerProvider` (`getPlayerProvider()`); fetch-user, choose-team, and welcome take `PlayerProvider*`. Awaken takes only `Player*` and draws default Alleycat art.

## Risks / Trade-offs

- [Name collision with `game/player.hpp` `Player`] → `PlayerV2` name + deprecation comment on `Player`.
- [Default `getPlayer()` masks missing profile bugs] → Acceptable stopgap; prefer `hasPlayer()` checks over time.
- [Wire metadata (`eventId`, `pdnCode`, `createdAtUnixMs`) now lives in a core type] → Acceptable; drop fields later if the backend allows.

## Migration Plan

1. Land `PlayerV2` + `PlayerProvider` in core; point alleycat-server and PlayerFetch at them.
2. Later: gameplay `Player` sync with `PlayerV2` at Quickdraw cutover.

## Open Questions

None blocking happy-path delivery.
