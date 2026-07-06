## MODIFIED Requirements

### Requirement: Pairing is a precondition to the Controller app
The `Controller` app (`TypedStateMachine<PDN>`) SHALL only be entered after pairing has completed successfully in `Quickdraw`. The `Controller` app SHALL NOT contain symbol exchange states.

#### Scenario: Controller app starts after successful pairing
- **WHEN** the PDN completes pairing with an FDN in `Quickdraw`
- **THEN** an app-level jump transitions the PDN from `Quickdraw` to the `Controller` app, entering at `ControllerRoutingState`

#### Scenario: Controller app does not contain pairing states
- **WHEN** the `Controller` app state machine is inspected
- **THEN** it contains no `SymbolState` or `SymbolWirelessManager`-dependent states

### Requirement: Controller app contains exactly two states
The `Controller` app state machine SHALL contain exactly two states: `ControllerRoutingState` (entry) and `ControllerState` (game state). No game-specific states SHALL be registered. Adding a new FDN game experience SHALL require no changes to the Controller app state machine.

#### Scenario: Controller app state machine is assembled
- **WHEN** the `Controller` app state machine is assembled
- **THEN** it contains exactly `ControllerRoutingState` and `ControllerState`, with one transition between them keyed on `LAUNCH_APP` receipt

#### Scenario: New FDN experience requires no Controller app changes
- **WHEN** a developer ships a new FDN game experience with a new `AppId`
- **THEN** the Controller app state machine assembly is unchanged; no new states are registered

### Requirement: ControllerRoutingState transitions to ControllerState on LAUNCH_APP
The state machine SHALL wire exactly one transition from `ControllerRoutingState` to `ControllerState`, triggered when `ControllerRoutingState` sets its transition flag on `LAUNCH_APP` receipt. The `AppId` SHALL be passed to `ControllerState` as context.

#### Scenario: LAUNCH_APP received in routing state
- **WHEN** `ControllerRoutingState` receives `LAUNCH_APP(appId)`
- **THEN** the state machine transitions to `ControllerState`, passing `appId` as context

### Requirement: FDN disconnect from any Controller app state returns to Quickdraw
FDN disconnect while the PDN is in either `ControllerRoutingState` or `ControllerState` SHALL result in an app-level jump back to `Quickdraw`. Each state owns its own disconnect detection.

#### Scenario: FDN disconnects in ControllerRoutingState
- **WHEN** the FDN disconnects while `ControllerRoutingState` is active
- **THEN** the PDN performs an app-level jump to `Quickdraw`

#### Scenario: FDN disconnects in ControllerState
- **WHEN** the FDN disconnects while `ControllerState` is active
- **THEN** `ControllerState` sets `transitionToQuickdrawState = true` and the PDN jumps to `Quickdraw`

### Requirement: Pairing seam between Quickdraw and Controller app
`SymbolMatchedState` in `Quickdraw` SHALL be the trigger point for the app jump to `Controller`. The exact mechanism (whether `SymbolMatchedState` listens for `LAUNCH_APP` or launches `Controller` unconditionally) SHALL be resolved before implementation.

#### Scenario: Pairing completes and Controller app is entered
- **WHEN** `SymbolMatchedState` signals completion of the pairing handshake
- **THEN** an app-level jump transitions the PDN to the `Controller` app at `ControllerRoutingState`
