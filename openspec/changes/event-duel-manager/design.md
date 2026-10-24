## Context

Event firmware duel flow reuses **existing** `QuickdrawPacket` / `QuickdrawCommand` and `QuickdrawWirelessManager`. Registration identity is **`PlayerProvider` / `PlayerV2`**. Upload wire format is **`QuickdrawMatchRecord`** → `POST /quickdraw-matches`.

Legacy `MatchManager` couples concerns we intentionally split:

```text
[ States ] ──getDuelButtonPush()──> [ MatchManager: radio + input + Match + JSON + didWin ]
```

Target:

```text
[ States ] ──onLocalPress / protocol──> [ MatchManagerV2 ]
              │                              ├── DuelSession
              │                              ├── DuelProtocol → QuickdrawWirelessManager
              │                              ├── evaluateDidSync (pure)
              │                              ├── QuickdrawMatchRecordBuilder
              │                              └── MatchRecordQueue → StorageInterface
```

## Goals / Non-Goals

**Goals:**

- Each module unit-testable on `native` without gameplay states.
- No mutable `Match` optional exposed to callers.
- Initiator/responder (A/B) as the only duel axis on the event path.
- Coordinator surface small enough for Node states to adopt without copying Quickdraw’s manager poke pattern.

**Non-Goals:**

- Replacing `QuickdrawWirelessManager` or changing packet layout in the first PR (see `link-peer-identity`).
- Button masher penalty, chain boost, or reaction-time stats on `Player`.

## Module layout (proposed)

| Module | Location (proposed) | Depends on |
|--------|---------------------|------------|
| `DuelLinkSide` | `event-duel/duel-link-side.hpp` | — |
| `DuelSession` | `event-duel/duel-session.hpp` | link side, UUIDBytes |
| `duel-sync-evaluator` | `event-duel/duel-sync-evaluator.hpp` | session inputs struct only |
| `DuelProtocol` | `event-duel/duel-protocol.hpp` | session, RDC, wireless |
| `QuickdrawMatchRecordBuilder` | `event-duel/quickdraw-match-record-builder.hpp` | session, PlayerV2 ×2 |
| `MatchRecordQueue` | `event-duel/match-record-queue.hpp` | storage, QuickdrawMatchRecord |
| `MatchManagerV2` | `event-duel/match-manager-v2.hpp` | all above |

All live under `src/pdn/game/event-duel/` to avoid colliding with legacy `match-manager.*`.

## Decisions

### DuelSession owns facts, not policy

Session holds: `matchId`, `opponentMac`, `localLinkSide`, `opponentPlayerId` (+ known flag), initiator/responder draw times and presence flags, handshake-ready, local pressed, remote result received, opponent-never-pressed, grace-without-local-press.

Session methods are **mutators with obvious names** (`recordLocalDrawTimeMs`, `applyRemoteDrawTimeMs`, `clear`). No scoring inside session.

### Sync vs complete

| Question | API | Used for |
|----------|-----|----------|
| Is the radio exchange finished? | `duelResultsAreComplete(inputs)` | Transition to result / retry UX |
| Did reactions sync? | `evaluateDidSync(inputs, windowMs)` | Win vs link-retry, finalize gate |

Never-pressed and grace paths **complete** the duel but **fail** sync.

### DuelProtocol does not know Player or storage

Protocol receives `PlayerProvider*` only for **outbound** identity on the wire (v1: truncated or placeholder id field — see link-peer-identity). Inbound commands update `DuelSession` only.

`QuickdrawCommand::isHunter` **means initiator** on the event path (`true` = initiator, `false` = responder). Role mismatch when both sides claim initiator on wire.

Same direct-peer gate as legacy: `SEND_MATCH_ID` requires `rdc->isDirectPeer(senderMac)`; post-handshake commands require `isFromActiveMatchOpponent`.

### States own button wiring (Node path)

**Decision:** Node duel states compute reaction time from `PlatformClock` and call:

- `matchManagerV2.onLocalDrawTimeMs(reactionMs)` (or session + protocol send)

**Not** `getDuelButtonPush()` registered inside coordinator `initialize()`.

*Exception:* If porting Quickdraw duel states verbatim for speed, a **deprecated adapter** may wrap the old callback pattern internally — spec requires the **logical** API above exists and is what new tests use.

### MatchRecordQueue

- Namespace: `node_matches` (or `event_duel_records`) — fixed in implementation PR.
- Fixed-size binary record blob per index; count in prefs.
- Max records: align with upload batch limits (100) or legacy 255 — **open:** pick 100 to match server.

Coordinator `finalizeSyncedMatch()`:

1. `evaluateDidSync` must be true
2. Builder produces `QuickdrawMatchRecord`
3. Queue append
4. Session clear

No HTTP in coordinator.

### QuickdrawMatchRecordBuilder

Inputs: session (times, match id, sides), local `PlayerV2`, opponent `PlayerV2` (must have ids).

Output mapping (unchanged product decision):

- A = initiator, B = responder
- `winnerPlayerId` = initiator id
- Teams from respective profiles at build time
- `occurredAtUnixMs` from `PlatformClock::unixMilliseconds()` when non-zero

### MatchManagerV2 facade (what states call)

Illustrative surface — names may adjust in implementation but **capabilities** must hold:

```text
setLocalLinkSide(DuelLinkSide)
bindOpponentPlayer(const PlayerV2&)   // until wire carries UUID
startMatchAsInitiator(opponentMac*)
onQuickdrawCommand(const QuickdrawCommand&)  // or listenForMatchEvents
isHandshakeReady()
onLocalDrawTimeMs(uint64_t)
onLocalGraceExpiredWithoutPress()
onRemoteNeverPressed()
duelResultsAreComplete()
didSync()
getFailedAttempts() / recordLinkOutcome(bool)
finalizeSyncedMatch() → bool
getRecordQueue() / readStoredRecord / clearStorage
```

No `didWin()`, `getCurrentMatch()`, `toJson()`, `setBoostProvider`, `setShootoutManager`.

### link-peer-identity (v1 gap)

Legacy packet `playerId[5]` cannot carry full UUID. Until proto/wire extension:

- Coordinator **`bindOpponentPlayer`** (or equivalent) MUST be called before finalize with opponent `PlayerV2.id`.
- Spec documents follow-up: extend handshake to exchange UUID (separate change).

Handshake tests may bind UUIDs explicitly; production Node **ConnectState** responsible for binding when peer profile is known.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| More types than monolith | Smaller PRs; pure evaluator tests first |
| States must wire buttons | Clear coordinator API; copy Quickdraw timing math only |
| Opponent UUID not on wire | Documented bind; follow-up packet change |
| Two duel implementations | Event path only until post-event QD refactor |

## Migration / delivery order

1. `duel-sync-evaluator` + `duel-session` tests + impl  
2. `QuickdrawMatchRecordBuilder` tests + impl  
3. `MatchRecordQueue` tests + impl  
4. `DuelProtocol` + integration with fake wireless  
5. `MatchManagerV2` coordinator + integration suite  

NodeNetwork states come **after** this change merges (see `node-network-event`).

## Open questions (for review)

1. **Record queue cap:** 100 (server batch) vs 255 (legacy prefs habit)?
2. **Button wiring:** Node states call `onLocalDrawTimeMs` only — confirm no masher penalty on event path?
3. **Outbound wire player id:** Send first 4 chars of UUID text, fixed `"PDN"`, or leave empty until wire extension?
4. **Facade name:** Keep public type `MatchManagerV2` vs rename to `EventDuelCoordinator`?
