## MODIFIED Requirements

### Requirement: ControllerRoutingState is a provided concrete state and the Controller app entry point
`ControllerRoutingState` SHALL be a provided concrete `ConnectState<PDN>` subclass used as the initial state of the `Controller` app state machine. It SHALL always be the entry point. It is **not** a subclass of `ControllerState`.

> **Planned deprecation:** `ControllerRoutingState` is designed to be deprecated once the comms architecture delivers a context object at device handshake time. That context will carry the `AppId`, eliminating the need for an explicit `LAUNCH_APP` exchange. Until that work lands, `ControllerRoutingState` serves as the bridge.

#### Scenario: Controller app starts after pairing
- **WHEN** the `Controller` app is entered after pairing
- **THEN** `ControllerRoutingState` is the first active state

### Requirement: ControllerRoutingState renders a neutral mount screen
On mount, `ControllerRoutingState` SHALL display a neutral "connecting…" screen. It SHALL NOT assume any app context on mount.

#### Scenario: Neutral screen on mount
- **WHEN** `ControllerRoutingState` mounts
- **THEN** the PDN displays a neutral connecting/waiting screen with no app-specific content

### Requirement: ControllerRoutingState handles LAUNCH_APP and transitions to ControllerState
`ControllerRoutingState` SHALL register `LAUNCH_APP` in its command registry. On receipt, it SHALL store the `AppId` and set a transition flag. The state machine SHALL wire a single transition from `ControllerRoutingState` to `ControllerState`. No game-specific states are registered as transition targets.

#### Scenario: FDN sends LAUNCH_APP
- **WHEN** `ControllerRoutingState` receives a `LAUNCH_APP` command
- **THEN** it stores the `AppId`, sets its transition flag, and the state machine transitions to `ControllerState` — the same `ControllerState` regardless of `AppId`

#### Scenario: Adding a new FDN experience requires no new PDN states
- **WHEN** a developer adds a new FDN game experience with a new `AppId`
- **THEN** no new PDN state classes are created and `ControllerRoutingState` is unchanged; the `AppId` is simply passed to `ControllerState` as context

### Requirement: FDN fully controls which app the PDN runs
The PDN SHALL NOT decide which app to run based on local state. The `Controller` app SHALL wait for a `LAUNCH_APP` command from the FDN before transitioning to `ControllerState`.

#### Scenario: PDN waits for FDN app selection
- **WHEN** the `Controller` app is active and no `LAUNCH_APP` has been received
- **THEN** the PDN remains in `ControllerRoutingState` displaying the neutral screen

### Requirement: FDN disconnect in ControllerRoutingState returns to Quickdraw
If the FDN disconnects while `ControllerRoutingState` is active, the PDN SHALL perform an app-level jump back to `Quickdraw`.

#### Scenario: FDN disconnects in ControllerRoutingState
- **WHEN** the FDN disconnects while `ControllerRoutingState` is active
- **THEN** the PDN performs an app-level jump to `Quickdraw`
