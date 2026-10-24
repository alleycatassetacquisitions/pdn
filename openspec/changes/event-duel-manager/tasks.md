## 0. Review

- [ ] 0.1 Reviewer sign-off on capability specs in this change
- [ ] 0.2 Resolve open questions in design.md

## 1. duel-sync-evaluator + duel-session

- [ ] 1.1 Native tests: completion vs sync, window edge, never-pressed, grace-without-press
- [ ] 1.2 Implement `DuelSession` + `duel-sync-evaluator` (headers only, no radio)

## 2. quickdraw-match-record-builder

- [ ] 2.1 Tests: A/B mapping, winner = initiator, nil teams omitted at codec boundary
- [ ] 2.2 Implement builder (pure function)

## 3. match-record-queue

- [ ] 3.1 Tests: append, read, clear, full queue rejection
- [ ] 3.2 Implement binary prefs encoding

## 4. duel-protocol

- [ ] 4.1 Tests: direct-peer gate, opponent MAC gate, initiator/responder wire mapping, role mismatch
- [ ] 4.2 Implement protocol adapter (fake wireless in tests)

## 5. match-manager-v2 coordinator

- [ ] 5.1 Tests: failed_attempts per MAC, finalize guard, two-device integration with bindOpponentPlayer
- [ ] 5.2 Implement facade wiring modules together
- [ ] 5.3 `pio test -e native` green

## 6. Follow-up (separate change)

- [ ] 6.1 Wire UUID in handshake packet(s)
- [ ] 6.2 NodeNetwork states consume coordinator API
