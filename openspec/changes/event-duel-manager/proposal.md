## Why

Legacy `MatchManager` is a monolith: ESP-NOW protocol, button callbacks, hunter/bounty scoring, chain boost, shootout exceptions, player stat mutation, UI snapshots, and JSON persistence in one class. Metagame / NodeNetwork needs **sync-window scoring**, **PlayerV2 + `QuickdrawMatchRecord`**, and **no Quickdraw tournament coupling** — a poor fit to extend in place.

This change defines a **decomposed event duel stack** (session, pure sync evaluation, protocol adapter, record queue, thin coordinator) deliverable and testable **before** NodeNetwork states or upload wiring — same boundary as standalone `PlayerFetchApp`.

## What Changes

- **`DuelSession`** — in-memory duel facts (match id, link side, draw times, completion flags); no I/O.
- **`duel-sync-evaluator`** — pure functions: exchange complete vs sync success (`NODE_SYNC_WINDOW_MS`).
- **`DuelProtocol`** — maps `QuickdrawCommand` ↔ session updates; RDC direct-peer gate; `isHunter` wire bit = initiator.
- **`MatchRecordQueue`** — append/read/clear `QuickdrawMatchRecord` in prefs (binary blob); no JSON `Match`.
- **`QuickdrawMatchRecordBuilder`** — builds upload record from session + local/opponent `PlayerV2` (A = initiator, B = responder, winner = initiator id).
- **`MatchManagerV2`** — thin coordinator states talk to; **does not** own button lambdas, `Player` stats, shootout, or boost.
- **`link-peer-identity`** — how opponent `PlayerV2.id` is established for finalize (wire gap documented; v1 may require explicit bind until packet carries UUID).

## Non-Goals

- Changing legacy `MatchManager` or Quickdraw states.
- Shootout, chain duel, symbol match, or competitive `didWin()` on this stack.
- HTTP upload (`MatchesApi`) — NodeNetwork upload state consumes the queue later.
- Device-side `delta_ms` or custom proto fields.
- Refactoring Quickdraw to use this stack after the event.

## Capabilities

### New Capabilities

| Capability | Responsibility |
|------------|----------------|
| `duel-session` | In-memory duel state, initiator/responder time axes |
| `duel-sync-evaluator` | Pure sync window + completion predicates |
| `duel-protocol` | ESP-NOW handshake and draw exchange |
| `match-record-queue` | Persist pending `QuickdrawMatchRecord` batches |
| `quickdraw-match-record-builder` | Session + profiles → record |
| `match-manager-v2` | Facade API for Node gameplay states |
| `link-peer-identity` | Opponent UUID availability for finalize |

### Modified Capabilities

None (NodeNetwork references this change; see `node-network-event`).

## Impact

- New headers/sources under `src/pdn/game/event-duel/` (exact paths in design.md).
- `test/test_core/` — unit tests per module; integration tests on coordinator + fake radio.
- No `quickdraw.cpp` / legacy match manager edits in this change.

## Dependency

- **Requires merged:** `PlayerV2`, `PlayerProvider`, `QuickdrawMatchRecord`, `QuickdrawMatchCodec` (alleycat device API client + player interface).
