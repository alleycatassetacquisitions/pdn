## 1. Specs

- [x] 1.1 Add `node-network-event` openspec change (proposal, design, capabilities)
- [ ] 1.2 Review specs with team before PR 1 merge

## 2. Dependency: event-duel-manager

- [ ] 2.1 Review and merge [`event-duel-manager`](../event-duel-manager/tasks.md) before Node gameplay PRs

## 3. NodeNetwork shell (PR 2)

- [ ] 3.1 `NodeNetwork` class + app id constant
- [ ] 3.2 Embed `PlayerFetchApp`; shared `PlayerProvider`
- [ ] 3.3 Wire `QuickdrawWirelessManager` → `MatchManagerV2::listenForMatchEvents` only
- [ ] 3.4 Transition `PlayerFetchApp::readyForGameplay()` → first Node gameplay state
- [ ] 3.5 Native or harness test for handoff predicate (if feasible)

## 4. Duel states (PR 3)

- [ ] 4.1 `node-network-states.hpp` ID block
- [ ] 4.2 Port idle + connect/jack rules (OUTPUT initiates)
- [ ] 4.3 Port duel countdown → result chain
- [ ] 4.4 `NodeWin` on sync; `NodeLinkRetry` on fail (no upload transition)
- [ ] 4.5 `node-network-resources.hpp` event palette / copy

## 5. Upload + firmware (PR 4)

- [ ] 5.1 `NodeUploadMatches` using `MatchesApi::uploadQuickdrawMatches`
- [ ] 5.2 Rejection logging / retry UX
- [ ] 5.3 PlatformIO event env + `main` default app = NodeNetwork
- [ ] 5.4 `pio run` event env; native tests green

## 6. Validation (PR 5)

- [ ] 6.1 Two-PDN jack cable soak
- [ ] 6.2 Pin fetch + sync upload + fail-without-upload paths
