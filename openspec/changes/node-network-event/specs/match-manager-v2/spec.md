## REMOVED Requirements

### Requirement: MatchManagerV2 monolith spec
**Reason:** Superseded by decomposed capabilities in [`event-duel-manager`](../../event-duel-manager/proposal.md) (`duel-session`, `duel-protocol`, `match-record-queue`, `match-manager-v2` facade, etc.).

**Migration:** NodeNetwork SHALL depend on the event duel stack change merging first; gameplay states consume the coordinator API defined there.
