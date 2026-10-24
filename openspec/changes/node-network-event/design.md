## Context

- **Registration:** `PlayerFetchApp` + `PlayerProvider` + `PlayerV2` (merged). Protobuf lookup only.
- **Upload client:** `QuickdrawMatchRecord`, `QuickdrawMatchCodec`, `MatchesApi::uploadQuickdrawMatches` (merged).
- **Radio:** Existing `QuickdrawWirelessManager` and `QuickdrawCommand` duel packets; `RemoteDeviceCoordinator` for jack/direct peer.
- **Legacy:** `MatchManager` + `Match` JSON + hunter/bounty competitive `didWin()` stay on Quickdraw.

## Goals / Non-Goals

**Goals:**

- One event app (`NodeNetwork`) with embedded fetch → gameplay → upload.
- `MatchManagerV2` testable on native without Arduino gameplay states.
- Upload records match [device_api.proto `QuickdrawMatch`](lib/alleycat-server/proto/device_api.proto) via existing codec.

**Non-Goals:**

- Unifying gameplay `Player` with `PlayerV2` across Quickdraw.
- Server or proto changes for sync metadata on this pass.

## Decisions

### NodeNetwork embeds PlayerFetch

Mirror Quickdraw:

```text
NodeNetwork::populateStateMap()
  PlayerFetchApp (pin → fetch → welcome → awaken)
  → transition on readyForGameplay()
  → Node gameplay states (idle, duel, …)
```

`NodeNetwork` constructs and holds `PlayerProvider`; passes the same pointer into `PlayerFetchApp` and `MatchManagerV2`.

*Why:* Single app id for the event, shared session profile, no multi-app switch in `main`.

### Event duel stack (separate change)

Duel logic is specified in **`openspec/changes/event-duel-manager/`** (session, sync evaluator, protocol, record queue, coordinator). NodeNetwork **consumes** `MatchManagerV2` from that change; do not re-spec the monolith here.

*Why:* Same boundary as standalone PlayerFetch — reviewable PR, native tests before states.

### Wire mapping for QuickdrawMatchRecord

| Field | Source |
|-------|--------|
| `matchId` | Shared UUID from duel handshake (same on both devices) |
| `playerAId` | Initiator (`PlayerV2.id` on OUTPUT side) |
| `playerBId` | Responder |
| `playerATeamId` / `playerBTeamId` | Match-time team from each side’s `PlayerV2` (omit if nil) |
| `playerADrawTimeMs` / `playerBDrawTimeMs` | Initiator / responder reaction times |
| `winnerPlayerId` | **Initiator id** (required by proto; front end disregards for sync UX) |
| `occurredAtUnixMs` | Wall clock when match completes (same source as legacy upload where available) |

Participant **A** is always initiator, **B** responder — both devices MUST use the same convention when encoding.

*Why:* Server and front end already consume standard quickdraw matches; sync is derived from times on FE.

### Sync failure

No `finalizeMatch`, no prefs row, no HTTP. States transition to soft-fail UX then idle.

### Storage

New prefs namespace or key prefix for **`QuickdrawMatchRecord`** (binary or compact encoding TBD in implementation). Reuse batch read pattern from legacy match upload states where practical.

### Fresh state port

Implement `node-network-states/` by copying behavior from Quickdraw states, not from `metagame` branch files.

## Risks / Trade-offs

- [Two duel implementations] → Accept until post-event Quickdraw refactor.
- [Handshake still uses legacy packet fields e.g. isHunter bit] → Map to initiator/responder locally; do not use for scoring.
- [Clock for `occurredAtUnixMs`] → Depends on HTTP date sync elsewhere; document if zero until sync lands.

## Migration Plan

1. Land specs + `MatchManagerV2` tests + implementation (PR 1).
2. NodeNetwork shell + fetch handoff (PR 2).
3. Duel states (PR 3).
4. Upload + event env (PR 4).
5. Hardware soak (PR 5).

## Open Questions

- Exact prefs encoding for `QuickdrawMatchRecord` queue (binary struct vs minimal JSON) — decide in PR 1 implementation.
- Whether `failed_attempts` is displayed on idle only or also sent later — currently **device-local only**.
