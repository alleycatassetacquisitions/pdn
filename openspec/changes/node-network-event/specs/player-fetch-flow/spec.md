## MODIFIED Requirements

### Requirement: No external gameplay handoff
PlayerFetch SHALL NOT add transitions from this app to Quickdraw Idle or other Quickdraw gameplay states. When PlayerFetch is embedded in **NodeNetwork**, `readyForGameplay()` MAY become true after awaken and SHALL be consumed by **NodeNetwork** to enter Node gameplay. Quickdraw SHALL continue to ignore PlayerFetch.

#### Scenario: After awaken on server path in NodeNetwork
- **WHEN** PlayerFetch is embedded in NodeNetwork and awaken completes
- **THEN** NodeNetwork transitions to Node gameplay
- **AND** Quickdraw does not transition to Idle as a result of PlayerFetch

#### Scenario: Standalone PlayerFetch tests
- **WHEN** PlayerFetch is exercised without NodeNetwork parent
- **THEN** no transition to external gameplay occurs
