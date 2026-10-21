## Context

- **Wire:** `DevicePlayerResponse` decodes into hand-written `DevicePlayer` (`device-player-decoder.cpp`). Nanopb types live only in `device_api.pb.h`.
- **Session today:** `PlayerFetchSession` stores a `DevicePlayer` by value; welcome reads name from it; awaken currently walks affiliations (to be replaced with default Alleycat art for now).
- **Gameplay today:** `game/player.hpp` `Player` is a separate concrete class (PIN, stats, legacy allegiance enum, hunter). It is not the registration profile.
- **Dependency:** `alleycat-server` already depends on `core` (`library.json`), so `DevicePlayer` can inherit from a core interface without inverting layers.

## Goals / Non-Goals

**Goals:**

- One core header home for registration **value types** (`Team`, `Affiliation`) and **`PlayerInterface`**.
- **`PlayerProvider`** as the session pattern (optional profile, set/clear, const access).
- **`DevicePlayer` implements `PlayerInterface`** with zero protobuf/schema changes.
- Native tests proving decode → `DevicePlayer` satisfies the interface and provider round-trip.

**Non-Goals:**

- Replacing or merging gameplay `Player` with `PlayerInterface` in this change (deprecation comment on `Player` only).
- Retaining wire metadata (`eventId`, `pdnCode`, `createdAtUnixMs`) in session storage — interface copy intentionally drops them; struct fields may be removed later when backend allows.
- PlayerFetch state refactors beyond happy path (can follow immediately after core lands).
- Affiliation-driven awaken art in PlayerFetch (use default Alleycat art until needed).

## Decisions

### `player-types.hpp` owns data + interface

Put **`Team`**, **`Affiliation`**, and **`PlayerInterface`** in `lib/core/include/game/player-types.hpp`. Keep the file free of Arduino/HTTP includes so both firmware and native tests can include it.

`PlayerInterface` exposes:

| Surface | Notes |
|--------|--------|
| `getPlayerId()` | Server UUID string; empty when unknown |
| `getName()` | Display name |
| `getTeam()` | `const Team&` — app assumes at least one team on stored profiles |
| `getAffiliations()` | `const std::vector<Affiliation>&` |
| `isActive()` | Server `active` flag |

Wire-only fields on `DevicePlayer` stay on the decode type for now but are **not** part of `PlayerInterface` and are **not** copied into session storage via `setPlayer`.

*Why:* App layer only needs registration display and side selection vocabulary, not event metadata.

*Alternative:* Separate `player-interface.hpp`. Rejected for now — two headers with little benefit at this size.

### Single team on the interface

Current API has `teamId` / `teamName`. **`DevicePlayer::getTeam()`** returns a **`Team`** built from those members. Happy-path PlayerFetch and demo stubs populate team fields; multi-team server models are out of scope.

*Alternative:* Vector of teams. Rejected for this change — single team matches product assumption.

### Affiliation value types in core

Define **`Affiliation`** in core with the same logical fields as today’s `DevicePlayerAffiliation` (category id/name, affiliation id/name). **`DevicePlayer`** SHOULD store `std::vector<Affiliation>` and drop the duplicate struct when applying this change (decoder sets string fields on core type; no nanopb regen).

### `DevicePlayer : public PlayerInterface`

Public inheritance on the existing struct; implement virtual getters as **`const override`** methods reading members.

*Protobuf safety:* Codegen never touches `device-player.hpp`. Decoder continues to write struct fields; only method bodies and member types change where needed.

### `PlayerProvider` abstract class in `player-provider.hpp`

```text
hasPlayer() const -> bool
getPlayer() const -> const PlayerInterface&   // stored profile, or default + LOG_E when empty
setPlayer(const PlayerInterface& profile)     // copies interface fields only into stored DevicePlayer
clear()
```

Default storage in concrete implementations (e.g. migrated **`PlayerFetchSession`**) is **`DevicePlayer`**.

When **`!hasPlayer()`**, **`getPlayer()`** returns a stable **default** `PlayerInterface` (member `DevicePlayer` or equivalent), logs **`LOG_E`**, and does not assert or crash — stopgap until all call sites guard with `hasPlayer()`.

### `setPlayer(const PlayerInterface&)` copies via interface

No downcast. Copy **`getPlayerId()`**, **`getName()`**, **`getTeam()`**, affiliations, **`isActive()`** into stored `DevicePlayer`. Do not copy or preserve metadata fields on the stored object.

Fetch success may call **`setPlayer(devicePlayer)`** using the decoded value as `PlayerInterface&`; only interface fields land in session storage.

### PlayerFetch awaken (follow-up wiring)

Replace affiliation scan with **default Alleycat** awaken art for now.

## Risks / Trade-offs

- [Name collision with `game/player.hpp` `Player`] → **`PlayerInterface`** + brief deprecation comment on **`Player`**.
- [Default `getPlayer()` masks missing profile bugs] → Acceptable stopgap; prefer `hasPlayer()` checks over time.
- [Virtual interface on registration paths] → Acceptable; not duel-loop hot.
- [Slicing if code stores `PlayerInterface` by value] → Provider holds concrete `DevicePlayer`.

## Migration Plan

1. Land core types + interface + provider + `DevicePlayer` overrides + unit tests.
2. Replace `DevicePlayerAffiliation` with **`Affiliation`** in alleycat-server (decoder unchanged field-wise).
3. Change `PlayerFetchSession` to implement `PlayerProvider`; update states to use `getPlayer()` / `setPlayer`; simplify awaken art.
4. Later: drop metadata fields from `DevicePlayer` when backend allows; gameplay `Player` sync at Quickdraw cutover.

## Open Questions

None blocking happy-path delivery.
