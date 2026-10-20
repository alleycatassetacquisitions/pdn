## Context

- **Wire:** `DevicePlayerResponse` decodes into hand-written `DevicePlayer` (`device-player-decoder.cpp`). Nanopb types live only in `device_api.pb.h`.
- **Session today:** `PlayerFetchSession` stores a `DevicePlayer` by value; welcome/awaken read affiliations and name from it.
- **Gameplay today:** `game/player.hpp` `Player` is a separate concrete class (PIN, stats, legacy allegiance enum, hunter). It is not the registration profile.
- **Dependency:** `alleycat-server` already depends on `core` (`library.json`), so `DevicePlayer` can inherit from a core interface without inverting layers.

## Goals / Non-Goals

**Goals:**

- One core header home for registration **value types** and **`PlayerInterface`**.
- **`PlayerProvider`** as the session pattern (optional profile, set/clear, const access).
- **`DevicePlayer` implements `PlayerInterface`** with zero protobuf/schema changes.
- Native tests proving decode → `DevicePlayer` satisfies the interface and provider round-trip.

**Non-Goals:**

- Replacing or merging gameplay `Player` with `PlayerInterface` in this change.
- Moving `DevicePlayer` struct fields into core (inheritance adapter on existing struct is enough for now).
- PlayerFetch state refactors (can follow immediately after core lands).
- Demo/local-only profiles that are not server-shaped (stub can use empty provider or a test double later).

## Decisions

### `player-types.hpp` owns data + interface

Put **`PlayerTeamAssignment`**, **`PlayerAffiliation`**, and **`PlayerInterface`** in `lib/core/include/game/player-types.hpp`. Keep the file free of Arduino/HTTP includes so both firmware and native tests can include it.

`PlayerInterface` exposes:

| Surface | Notes |
|--------|--------|
| `getPlayerId()` | Server UUID string; empty when unknown |
| `getName()` | Display name |
| `getTeams()` | `const std::vector<PlayerTeamAssignment>&` — empty when unassigned |
| `getAffiliations()` | `const std::vector<PlayerAffiliation>&` |
| `isActive()` | Server `active` flag |

Use **`const` reference** return for collections so implementors can return member vectors without allocation per call.

*Why:* User asked for a types home, not a sprawling set of one-type-per-header files. Interface stays next to the structs it returns.

*Alternative:* Separate `player-interface.hpp`. Rejected for now — two headers with circular include risk and little benefit at this size.

### Teams as a vector with today’s single-team server

Current API has `teamId` / `teamName`. **`DevicePlayer::getTeams()`** returns an empty vector when `!hasTeam()`, otherwise a vector of one `PlayerTeamAssignment`.

*Why:* Interface matches user wording (“teams”) and leaves room for multi-team events without another breaking API later.

*Alternative:* Single optional team on the interface. Rejected — would require a second breaking change when the server adds multiples.

### Affiliation value types in core, not aliases of `DevicePlayerAffiliation`

Define **`PlayerAffiliation`** in core with the same logical fields (category id/name, affiliation id/name). **`DevicePlayer`** keeps `std::vector<DevicePlayerAffiliation>` for storage; interface methods **map or expose** compatible data (either return a cached `std::vector<PlayerAffiliation>` built on read, or store as `PlayerAffiliation` and drop the duplicate struct — prefer **one stored form** in `device-player.hpp` aligned to core types to avoid dual structs long term).

*Implementation note for apply phase:* Prefer replacing `DevicePlayerAffiliation` with `PlayerAffiliation` typedef or using `PlayerAffiliation` as the member type if decoder can populate it without nanopb changes (decoder sets string fields on core type).

### `DevicePlayer : public PlayerInterface`

Public inheritance on the existing struct; implement virtual getters as **`const override`** methods reading members.

*Why:* Callers holding `DevicePlayer` get both decode target and interface; `PlayerProvider` can store `DevicePlayer` by value.

*Protobuf safety:* Codegen never touches `device-player.hpp`. Decoder continues to write struct fields; only method bodies added.

*Alternative:* Adapter class wrapping `DevicePlayer`. Rejected — extra indirection and lifetime glue for every fetch.

### `PlayerProvider` abstract class in `player-provider.hpp`

```text
hasPlayer() const -> bool
getPlayer() const -> const PlayerInterface&   // precondition: hasPlayer()
setPlayer(const PlayerInterface& profile)     // copies fields into stored DevicePlayer/concrete
clear()
```

Default storage in concrete implementations (e.g. **`SessionPlayerProvider`** or migrated **`PlayerFetchSession`**) is **`DevicePlayer`**.

*Why:* Matches session semantics from PlayerFetch; tests can use a small concrete provider without the app.

*Alternative:* Template provider `PlayerProvider<T>`. Rejected — firmware style favors explicit types and raw ownership.

### `setPlayer(const PlayerInterface&)` copies via interface

Provider implementation downcasts is **not** used. **`setPlayer`** copies through **`getPlayerId()`**, **`getName()`**, teams, affiliations, **`isActive()`** into stored `DevicePlayer`.

*Why:* Allows tests to pass mock interfaces; avoids requiring concrete `DevicePlayer` at every call site.

## Risks / Trade-offs

- [Name collision with `game/player.hpp` `Player`] → Interface is explicitly **`PlayerInterface`**; documentation and includes must not alias it to `Player`.
- [Virtual interface on hot paths] → Registration is not duel-loop hot; acceptable.
- [Dual affiliation struct during migration] → Apply task should converge on **`PlayerAffiliation`** only in `DevicePlayer` members.
- [Slicing if code stores `PlayerInterface` by value] → Spec forbids storing interface by value; provider holds concrete `DevicePlayer`.

## Migration Plan

1. Land core types + interface + provider + `DevicePlayer` overrides + unit tests.
2. Optionally rename `DevicePlayerAffiliation` → use `PlayerAffiliation` in alleycat-server (decoder unchanged field-wise).
3. Change `PlayerFetchSession` to implement `PlayerProvider`; update PlayerFetch states to depend on `PlayerProvider*`.
4. Later: gameplay `Player` reads from provider or syncs selected fields at cutover to Idle.

## Open Questions

- Should `PlayerProvider::setPlayer` accept `DevicePlayer` overload for zero-copy move from fetch callback, or is interface copy sufficient? (Likely add **`setPlayer(const DevicePlayer&)`** on concrete session class only.)
- Event metadata (`eventId`, `pdnCode`, `createdAtUnixMs`) — not on `PlayerInterface` yet; add when gameplay or upload needs them.
