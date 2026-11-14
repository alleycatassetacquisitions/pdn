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

Link side (`INITIATOR` / `RESPONDER`) is an `enum class DuelLinkSide`. Success counts are stored per side. Player A in the upload record is the initiator.

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
| `command` | `OFFER`, `ACCEPT`, `ROLE_MISMATCH`, `TIME_CORRECTION`, `SYNC`, `SYNC_REPLY`, `SEQUENCE`, `INPUT_RESULT`, `OUTCOME`, `ABORT`, `ACK`, `INPUT_PRESS` |
| `senderIsInitiator` | sender's link side |
| `reliable` | `1` when the receiver must answer with `ACK` |
| `sequenceNumber` | reliable packets: sender-assigned id; `ACK`: the id being acknowledged |
| `matchId` | 16 bytes |
| `senderPlayerId` | 16 bytes |
| `senderTeamId` | 16 bytes, nil when unassigned |
| `stepIndex`, `pressCount` | `INPUT_RESULT`, `INPUT_PRESS` |
| `inputSuccess` | `INPUT_RESULT` |
| `stepPrompts[5]` | `SEQUENCE`; per step, initiator prompt in the low nibble, responder in the high. Nibble bits 0–1 = button count, bit 2 = first button secondary, bit 3 = second button secondary |
| `initiatorScore`, `responderScore` | `OUTCOME` |
| `syncOriginMs` | `SYNC` / `SYNC_REPLY`: initiator uptime when the probe was sent |
| `syncReceiveMs`, `syncTransmitMs` | `SYNC_REPLY`: responder uptime on receipt and on reply |
| `stepZeroAtMs` | `SEQUENCE`: step 0 start, already converted to the receiver's uptime |
| `occurredAtUnixMs` | `OFFER` and `TIME_CORRECTION`; zero otherwise |

There is no version byte and no backwards compatibility. Receive validates length and command, then hands a decoded value to the manager. Sends go through `WirelessManager::sendEspNowData`.

### Reliable delivery lives in the wireless layer

`NodeNetworkWirelessManager` offers `sendPacket` (fire and forget) and `sendReliable`. A reliable packet gets a sequence number and stays in a small pending table; `update()` (called every app loop by `NodeNetwork`) resends it every `NODE_RELIABLE_RETRY_MS` until an `ACK` arrives or `NODE_RELIABLE_MAX_ATTEMPTS` sends have failed. The outcome is reported through one delivery callback (`delivered` true or false). The receiver always answers a reliable packet with `ACK` and delivers it to the manager once; repeats (same MAC, match id, and sequence number) are acknowledged but not delivered again.

An `ACK` means the peer's wireless layer has the packet, not that the manager accepted it, so receivers store round data regardless of which state is mounted. The retry interval sits above the ESP-NOW driver's own link-level retries. Because the layer outlives any state, a packet sent just before a state change (for example `OUTCOME`) keeps being retried.

Reliable: `SEQUENCE`, `INPUT_RESULT`, `OUTCOME`, `ABORT`. Unreliable: `OFFER`/`ACCEPT`/`ROLE_MISMATCH`/`TIME_CORRECTION` (idle re-offers), `SYNC`/`SYNC_REPLY` (a resend would corrupt the round-trip measurement), `INPUT_PRESS` (display only; `INPUT_RESULT` carries the final count), and `ACK` itself.

### Gating (modeled on Quickdraw)

- `OFFER` is accepted only from a direct peer (`isDirectPeer`).
- Every other command is accepted only from the active session's opponent MAC with the active match id, after the handshake, and only from the side that sends it.

### Clock alignment uses uptime, not UTC

UTC on each device comes from the HTTP `Date` header (whole seconds, set at different times), so two devices disagree by up to a second or more. Game timing therefore uses `PlatformClock::milliseconds()` plus a measured offset. UTC is only used for the record timestamp.

During the countdown state the initiator sends `NODE_SYNC_PROBE_COUNT` `SYNC` probes, `NODE_SYNC_PROBE_INTERVAL_MS` apart. Each `SYNC_REPLY` gives one sample: `offset = ((T2 − T1) + (T3 − T4)) / 2`, `rtt = (T4 − T1) − (T3 − T2)`. The sample with the smallest round trip wins. If no sample has a round trip at or below `NODE_SYNC_MAX_RTT_MS` (75, configurable), the round is aborted. Arithmetic is modulo 2^32 and assumes the two uptimes differ by less than about 24 days.

### Round: a shared schedule

After a good sync the initiator generates the `NodeInputSequence`, picks `stepZero = now + NODE_COUNTDOWN_MS`, and sends one reliable `SEQUENCE` carrying every step's prompts and `stepZero` converted into the responder's uptime. The responder never needs the offset.

Each step holds one prompt per side, of zero to two buttons in press order. A step is drawn as: one player, one button (2/6); one player, two buttons (3/6); or both players (1/6), each independently getting one or two buttons. The player and every button are chosen at random.

A both-player step gets the both timing (`NODE_BOTH_INPUT_WINDOW_MS` 3000, `NODE_BOTH_STEP_GAP_MS` 3300) whatever its button counts; a one-player two-button step gets the double timing (`NODE_DOUBLE_INPUT_WINDOW_MS` 2100, `NODE_DOUBLE_STEP_GAP_MS` 2400); other steps use `NODE_INPUT_WINDOW_MS` (1400) and `NODE_STEP_GAP_MS` (1700). Step i starts at `stepZero` plus the earlier steps' windows and gaps, on each device's own clock. There is no per-step prompt message: both devices run the same schedule, so a message would be a second source of truth for when a step starts.

What each device shows during a step:

- Only the local player presses: NOW.
- Only the opponent presses: the opponent's prompt.
- Both press: each device shows its own prompt; neither shows NOW.

LEDs: off before step 0. During each window the six display LED pairs light in a green palette at `NODE_DUEL_LED_BRIGHTNESS` and drain to black over the window, last pair first, as a countdown. When the stage result is known the LEDs blink green (PASS) or red (MISS) twice, 450 on / 200 off / 450 on, then stay off; the shortest gap (1700 ms) fits the 1100 ms blink.

A one-button prompt is drawn at the image's default position; a two-button prompt is drawn at x=0 and x=64. A pressed button is drawn inverted, on the presser's own prompt and on the viewer's copy. The viewer learns of presses from `INPUT_PRESS` (sent after each correct press that does not finish the prompt) and `INPUT_RESULT`.

Presses are judged in order. A wrong button ends the local part as a miss at once; the last correct button ends it as a pass; an unfinished prompt misses when the window closes. A step passes only when every pressing side passes, and the step's result screen shows one PASS or MISS.

Each presser sends its part as a reliable `INPUT_RESULT` with its press count (both directions). In the outcome, a passed step counts for the initiator whenever the initiator pressed it (both-player steps included) and for the responder otherwise, so the two counts still sum to the steps passed.

The responder starts its countdown when `SEQUENCE` arrives, and both countdown screens are drawn from the time left until step 0, so they end together.

The initiator finishes once every step has a result, or `NODE_RESULT_GRACE_MS` after the last window closes; steps still without a result fail. It sends a reliable `OUTCOME` with the per-side counts and concludes only once the responder acknowledges it. If `OUTCOME` runs out of resends, the initiator aborts uncounted. The responder waits `NODE_OUTCOME_WAIT_MS` past the last window, longer than the grace plus every resend (checked at compile time), so a late copy cannot be acknowledged after it gave up. The round succeeds when the total is at least `NODE_SUCCESS_THRESHOLD` (4). Both devices show the score screen ("Score", "N/5") before upload on success or the retry flow on failure.

### Aborts are not counted

An abort clears the session, shows "Error / Retrying" for `NodeError::DISPLAY_DURATION_MS`, and returns to idle without touching the failed-attempt count. Whoever aborts sends a reliable `ABORT`; the peer aborts too. Abort cases:

- Initiator: no good sync sample, `SEQUENCE` delivery failed or was not acknowledged by step 0, or `OUTCOME` was never acknowledged.
- Responder: no `SEQUENCE` within `NODE_SEQUENCE_WAIT_MS` of entering the countdown, or a `SEQUENCE` whose step 0 is already past or implausibly far out.
- Either side: the session disappears mid-round (`ABORT` received).
- Responder: no `OUTCOME` by the last window plus `NODE_OUTCOME_WAIT_MS`.

A cable disconnect still returns straight to idle, as before.

### Input stays in states

States own all timing and button callbacks and call `onLocalInputResult`. The manager owns the schedule arithmetic (`msUntilStep`) and registers no buttons. No masher penalty.

### Storage

`MatchStorage` persists `QuickdrawMatchRecord` only, in its own namespace, never in the legacy `matches` namespace. Typical depth is 0 or 1 because upload is attempted right after each successful match. A fixed cap (`MAX_PENDING_RECORDS`, default 100, the server batch limit) bounds offline retry.

Records are stored as fixed-size binary built into a `std::string` by length, because the storage interface is string-based and a record contains zero bytes. Tests round-trip a record containing zeros.

### Failed attempts

Failures are a limit, not data. After `MAX_FAILED_ATTEMPTS` (3) failed duels in total with the same peer, `hasExhaustedAttempts()` is true and NodeNetwork transitions both devices to sleep. The count resets on a successful round or when the opponent MAC changes. It lives on the manager, not the session, so it survives `clearCurrentMatch()`. It is never uploaded.

### Finalize

`finalizeMatch()`: requires a registered local player, a known outcome, and a successful round. It builds the record from the session, appends to `MatchStorage`, then clears the session. No HTTP.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| New packet means event firmware cannot duel against Quickdraw builds | Intended; event image only |
| Devices disagree on the outcome | Only the initiator decides; the responder adopts `OUTCOME` or fails the round. Integration tests assert agreement. |
| Lost `INPUT_RESULT` or `OUTCOME` | Reliable layer retries; results still missing after the grace count as failures |
| `OUTCOME` arrives but every `ACK` is lost | Responder counts the round and the initiator aborts uncounted; on success one device still uploads the shared record. Needs eight lost ACKs in a row |
| Bad clock sync | Smallest-round-trip sample, rejected above `NODE_SYNC_MAX_RTT_MS`; the round aborts uncounted |
| Evaluator seam deferred | Success threshold applied in one place (`DuelSession::isRoundSuccess`) |
| Binary record through a string API | Round-trip test with embedded zeros |

## Delivery order

1. `DuelSession` and the round result (pure, no radio).
2. Record projection on the session.
3. `MatchStorage`.
4. `NodeNetworkPacket` encode/decode and `NodeNetworkWirelessManager`.
5. `MatchManagerV2` with a two-device integration suite.

## Open Questions

None.
