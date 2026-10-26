## Context

- **Registration:** `PlayerFetchApp` + `PlayerProvider` (owns a `Player` carrying server id, team, affiliations). Protobuf lookup only.
- **Upload client:** `QuickdrawMatchRecord`, `QuickdrawMatchCodec`, `MatchesApi::uploadQuickdrawMatches` (merged).
- **Radio:** the NodeNetwork duel packet and `NodeNetworkWirelessManager` from `node-network-duel-manager`; `RemoteDeviceCoordinator` for jack/direct peer.
- **Legacy:** `MatchManager` + `Match` JSON + hunter/bounty competitive `didWin()` stay on Quickdraw.

## Goals / Non-Goals

**Goals:**

- One event app (`NodeNetwork`) with embedded fetch → gameplay → upload.
- `MatchManagerV2` testable on native without Arduino gameplay states.
- Upload records match [device_api.proto `QuickdrawMatch`](lib/alleycat-server/proto/device_api.proto) via existing codec.

**Non-Goals:**

- Refactoring Quickdraw's use of `Player` or registration.
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

### NodeNetwork duel stack (separate change)

Duel logic is specified in **`openspec/changes/node-network-duel-manager/`** (session, wireless, storage, `MatchManagerV2`). NodeNetwork **consumes** `MatchManagerV2` from that change.

*Why:* Same boundary as standalone PlayerFetch — reviewable PR, native tests before states.

### Record mapping

Owned by `node-network-duel-manager` (`duel-session`): A = initiator, B = responder, `winnerPlayerId` = initiator id, both draw times uploaded, `occurredAtUnixMs` agreed in the handshake. The front end derives sync from the draw times.

### Sync failure and sleep

No `finalizeMatch`, no stored record, no HTTP. States show soft-fail UX then return to idle. When `MatchManagerV2::hasExhaustedAttempts()` is true (3 failed duels with the same peer), both devices transition to the sleep state instead of idle.

### Upload

`NodeUploadMatches` reads pending records from `MatchManagerV2`, uploads right after each successful match, and clears on success. Typical depth is one record.

### Fresh state port

Implement `node-network-states/` by copying behavior from Quickdraw states, not from `metagame` branch files.

## Risks / Trade-offs

- [Two duel implementations] → Accept until post-event Quickdraw refactor.
- [Clock for `occurredAtUnixMs`] → Set from the HTTP `Date` header after a successful fetch; the initiator will not start a duel if the clock is not positive.

## Migration Plan

1. Land specs + `MatchManagerV2` tests + implementation (PR 1).
2. NodeNetwork shell + fetch handoff (PR 2).
3. Duel states (PR 3).
4. Upload + event env (PR 4).
5. Hardware soak (PR 5).

## Open Questions

- Whether the idle screen shows the failed-attempt count before sleep, or only the soft-fail copy.
