## 0. Review

- [ ] 0.1 Reviewer sign-off on capability specs
- [ ] 0.2 Resolve open questions in design.md

## 1. duel-session

- [x] 1.1 Tests: prime/clear, initiator/responder time slots, completion flags, ignored duplicate draws
- [x] 1.2 Tests: record projection (A = initiator, winner = initiator, teams, agreed timestamp, refuses when incomplete)
- [x] 1.3 Implement `DuelLinkSide` and `DuelSession`

## 2. match-storage

- [x] 2.1 Tests: append, read, count, clear, capacity limit, round trip with embedded zero bytes, namespace isolation
- [x] 2.2 Implement `MatchStorage`

## 3. node-network-wireless

- [x] 3.1 Tests: packet encode/decode round trip, bad length, bad version
- [x] 3.2 Tests: direct-peer gate, opponent MAC and match id gate, role mismatch, duplicate draw ignored
- [x] 3.3 Tests: time correction (implausible offer, re-offer once, no loop, ordinary skew accepted, invalid responder clock)
- [x] 3.4 Add `PktType` value; implement `NodeNetworkPacket` and `NodeNetworkWirelessManager`

## 4. match-manager-v2

- [x] 4.1 Tests: sync window (inside, edge at 250, outside), never-pressed, grace without press
- [x] 4.2 Tests: finalize guard (not synced, incomplete), finalize appends one record and clears session
- [x] 4.3 Tests: failed-attempt limit (3), reset on success and on peer change, survives session clear
- [x] 4.4 Tests: two-device integration (handshake with UUIDs and teams, shared timestamp, both sides agree on sync, identical records)
- [x] 4.5 Implement `MatchManagerV2`
- [x] 4.6 `pio test -e native` green

## 5. Input-sequence round

- [x] 5.1 Packet (no version byte): `PROMPT`, `INPUT_RESULT`, `OUTCOME`, `OUTCOME_REQUEST`; step/score fields replace `drawTimeMs`
- [x] 5.2 `NodeInput` / `NodeInputSequence` and random generator
- [x] 5.3 `DuelSession` steps, results, outcome; record carries per-side success counts
- [x] 5.4 `MatchManagerV2` round API, outcome cache, `concludeRound`
- [x] 5.5 `NodeDuel` step runner and `NodeScore` screen
- [x] 5.6 Tests: session, manager, two-device integration (lost result, lost outcome), state tests
- [ ] 5.7 Hardware check: primary/secondary button images match the physical buttons

## 6. Sync and reliable round

- [x] 6.1 Packet: drop `PROMPT`/`OUTCOME_REQUEST`; add `SYNC`, `SYNC_REPLY`, `SEQUENCE`, `ABORT`, `ACK`, reliable flag, sequence number, sync and step-0 fields
- [x] 6.2 Generic reliable layer in `NodeNetworkWirelessManager` (ACK, resend, dedupe, delivery callback, `update`)
- [x] 6.3 `DuelSession`: sync samples, schedule, `SEQUENCE` and `OUTCOME` delivery flags
- [x] 6.4 `MatchManagerV2`: SYNC offset, `beginRound` with shared step 0, reliable `INPUT_RESULT`/`OUTCOME`/`ABORT`, uncounted abort
- [x] 6.5 States: countdown syncs and shares the schedule, `NodeDuel` runs the schedule, `NodeError` shows "Error / Retrying"; `NodeNetwork` pumps the wireless `update`
- [x] 6.6 Tests: reliable layer, sync, schedule, lost packets, abort, state flow
- [ ] 6.7 Hardware check: both devices show each step at the same moment; sync round trips stay under 75 ms
- [x] 6.8 Confirm threading: `EspNowDriver` queues receives on the WiFi task and dispatches them from `exec()` on the main loop, so `processPacket` and `update` never run concurrently

## 7. Two-button and both-player steps

- [x] 7.1 `NodePrompt` per side per step; generator mix 2/6 single, 3/6 double, 1/6 both-player; double timing (2100/2400) for one-player two-button steps, both timing (3000/3300) for both-player steps
- [x] 7.2 Packet: `pressCount`, `stepPrompts[5]` nibbles replace the button and presser masks; unreliable `INPUT_PRESS`
- [x] 7.3 `DuelSession`: per-side part results, both-player AND, initiator credit, opponent press count
- [x] 7.4 `NodeDuel`: in-order judging, wrong press ends the step, own prompt on both-player steps, x=0/x=64 layout, inverted pressed buttons; `Display::drawImageInverted`
- [x] 7.5 Tests: generator, timing, prompt codec, both-player scoring, `INPUT_PRESS`, duel drawing and judging
- [ ] 7.6 Run the native test suite and style check
- [ ] 7.7 Hardware check: inverted images, two-button layout, double timing feel

## 8. Follow-up (separate change)

- [ ] 8.1 Pluggable outcome evaluator, once designed
