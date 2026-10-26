## Context

- Local identity comes from `PlayerProvider::getPlayer()`: `getServerPlayerId()`, `getRegistrationTeam()`, `hasRegistrationProfile()`.
- Upload record is `QuickdrawMatchRecord` (`POST /quickdraw-matches`).
- `PlatformClock::unixMilliseconds()` is synced from the HTTP `Date` header after a successful player fetch, so it is non-zero once a player is registered.
- `RemoteDeviceCoordinator::isDirectPeer(mac)` and `getPeerMac(port)` identify the cable peer. A larger RDC refactor is coming; this change uses only those two calls.
- `StorageInterface` is string-based (`read`/`write` with `std::string`, `readUChar`/`writeUChar`).

## Shape

```text
[ Node states ] ──> MatchManagerV2
                      ├── DuelSession                 (facts + record projection)
                      ├── NodeNetworkWirelessManager    (NodeNetworkPacket <-> manager)
                      └── MatchStorage                (pending QuickdrawMatchRecord)
```

Four units. No intermediate types beyond the packet struct and a small command enum.

## Decisions

### Initiator and responder are the only duel axis

Link side (`INITIATOR` / `RESPONDER`) is an `enum class DuelLinkSide`. Draw times are stored per side. Player A in the upload record is the initiator.

Link side is chosen by the caller when a duel starts (OUTPUT jack = initiator, INPUT jack = responder) and is fixed for the life of the session. If the jack role changes while a session is active, the caller aborts with `clearCurrentMatch()`; the session never re-derives its side.

### Agreed timestamp

The initiator stamps `occurredAtUnixMs` from `PlatformClock::unixMilliseconds()` when it sends the offer. The responder adopts that value from the packet. Both devices therefore build identical records for one match id, which suits the server's first-accepted-upload-wins rule.

If the initiator's clock reads 0, it refuses to start a match. The responder rejects an offer whose timestamp is 0 or negative.

If the offered timestamp is orders of magnitude below the responder's (`offer * TIME_IMPLAUSIBLE_RATIO < responder`, ratio 1000; a real unix-ms value is around 1.7e12, a boot-relative one far smaller), the initiator never got a valid time. The responder primes nothing and replies `TIME_CORRECTION` with its own unix time. The initiator re-sends the offer once with that value. The correction applies to the duel only, not to the platform clock.

### Identity travels in the handshake

`OFFER` carries the initiator's player id and team id. `ACCEPT` carries the responder's. After the handshake each session holds both participants' ids and team ids, so no bind step and no dependence on the RDC for identity.

A nil team id means unassigned and passes through unchanged.

### NodeNetworkPacket

One packed struct, new `PktType`. Fields:

| Field | Notes |
|-------|-------|
| `protocolVersion` | reject on mismatch |
| `command` | `OFFER`, `ACCEPT`, `ROLE_MISMATCH`, `TIME_CORRECTION`, `DRAW`, `NEVER_PRESSED` |
| `senderIsInitiator` | sender's link side |
| `matchId` | 16 bytes |
| `senderPlayerId` | 16 bytes |
| `senderTeamId` | 16 bytes, nil when unassigned |
| `drawTimeMs` | `DRAW` / `NEVER_PRESSED` |
| `occurredAtUnixMs` | `OFFER` and `TIME_CORRECTION`; zero otherwise |

Receive validates length and version, then hands a decoded value to the manager. Sends go through `WirelessManager::sendEspNowData`.

### Gating (modeled on Quickdraw)

- `OFFER` is accepted only from a direct peer (`isDirectPeer`).
- `ACCEPT`, `ROLE_MISMATCH`, `DRAW`, `NEVER_PRESSED` are accepted only from the active session's opponent MAC with the active match id.
- A second `DRAW` or `NEVER_PRESSED` from a side that already reported is ignored.

### Sync decision lives in the manager

`didSync()` is `abs(initiatorTime - responderTime) <= NODE_SYNC_WINDOW_MS` (250) with both times present and neither never-pressed/grace flag set. It is a private-ish function on `MatchManagerV2` with its own unit tests. No evaluator interface yet; extracting one later should only move this function.

### Input stays in states

States measure reaction time and call `onLocalDrawTimeMs`. No button lambdas in the manager. No masher penalty.

### Storage

`MatchStorage` persists `QuickdrawMatchRecord` only, in its own namespace, never in the legacy `matches` namespace. Typical depth is 0 or 1 because upload is attempted right after each successful match. A fixed cap (`MAX_PENDING_RECORDS`, default 100, the server batch limit) bounds offline retry.

Records are stored as fixed-size binary built into a `std::string` by length, because the storage interface is string-based and a record contains zero bytes. Tests round-trip a record containing zeros.

### Failed attempts

Failures are a limit, not data. After `MAX_FAILED_ATTEMPTS` (3) failed duels in total with the same peer, `hasExhaustedAttempts()` is true and NodeNetwork transitions both devices to sleep. The count resets on a successful sync or when the opponent MAC changes. It lives on the manager, not the session, so it survives `clearCurrentMatch()`. It is never uploaded.

### Finalize

`finalizeMatch()`: requires a registered local player, a complete session, and `didSync()`. It builds the record from the session, appends to `MatchStorage`, then clears the session. No HTTP.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| New packet means event firmware cannot duel against Quickdraw builds | Intended; event image only |
| Both devices decide sync independently | Both hold the same two times after exchange, so the outcome is symmetric. Integration tests assert it. |
| Evaluator seam deferred | Sync check isolated in one function with its own tests |
| Binary record through a string API | Round-trip test with embedded zeros |

## Delivery order

1. `DuelSession` and the sync check (pure, no radio).
2. Record projection on the session.
3. `MatchStorage`.
4. `NodeNetworkPacket` encode/decode and `NodeNetworkWirelessManager`.
5. `MatchManagerV2` with a two-device integration suite.

## Open Questions

None.
