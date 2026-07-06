## ADDED Requirements

### Requirement: FdnGameRoutingState is a provided concrete FdnGameState
`FdnGameRoutingState` SHALL be a provided concrete `FdnGameState` subclass used as the entry point of the FDN game state machine. It SHALL be used by including it — developers do not write their own routing state.

> **Planned deprecation:** `FdnGameRoutingState` is designed to be deprecated once the comms architecture delivers a context object at device handshake time. That context will carry the `AppId`, eliminating the need for the routing exchange. Until that work lands, `FdnGameRoutingState` serves as the bridge.

#### Scenario: FDN game state machine starts
- **WHEN** the FDN game session begins
- **THEN** `FdnGameRoutingState` is the first active state

### Requirement: FdnGameRoutingState supports direct launch mode
In direct launch mode, `FdnGameRoutingState` SHALL immediately broadcast `LAUNCH_APP(targetAppId)` to connected PDNs on mount and transition to the corresponding concrete game state. No user interaction or `SELECTION_CONFIRMED` is required.

#### Scenario: Direct launch on mount
- **WHEN** `FdnGameRoutingState` is configured for direct launch with a known `AppId`
- **THEN** it broadcasts `LAUNCH_APP(targetAppId)` and sets a transition flag to the target game state on mount

### Requirement: FdnGameRoutingState supports interactive selection mode
In interactive selection mode, `FdnGameRoutingState` SHALL broadcast a `COMMAND_SET` containing available apps to connected PDNs. It SHALL register `SELECTION_CONFIRMED` in its input registry. On receipt of `SELECTION_CONFIRMED(appId)`, it SHALL broadcast `LAUNCH_APP(appId)` and transition to the corresponding game state.

#### Scenario: Interactive selection — PDN selects an app
- **WHEN** `FdnGameRoutingState` is in interactive mode and a connected PDN sends `SELECTION_CONFIRMED(appId)`
- **THEN** it broadcasts `LAUNCH_APP(appId)` to all connected PDNs and transitions to the game state registered for that `AppId`

#### Scenario: Unknown AppId received in interactive mode
- **WHEN** `SELECTION_CONFIRMED` carries an `AppId` not registered as a transition target
- **THEN** a warning is logged and `FdnGameRoutingState` remains active — it does not crash or enter an undefined state

### Requirement: FDN fully controls app selection — PDN never decides unilaterally
The FDN routing state is always in control. In interactive mode, the PDN confirms a selection from a list the FDN provided. The FDN still sends `LAUNCH_APP` — the PDN does not transition on its own.

#### Scenario: Interaction is FDN-mediated
- **WHEN** a player confirms a selection on the PDN
- **THEN** the FDN receives `SELECTION_CONFIRMED`, validates it, and sends `LAUNCH_APP` — the PDN only transitions after receiving `LAUNCH_APP` from the FDN
