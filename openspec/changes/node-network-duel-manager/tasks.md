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

## 5. Follow-up (separate change)

- [ ] 5.1 NodeNetwork states consume `MatchManagerV2`
- [ ] 5.2 Pluggable outcome evaluator, once designed
