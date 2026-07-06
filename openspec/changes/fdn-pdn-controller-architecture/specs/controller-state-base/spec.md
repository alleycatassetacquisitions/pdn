## MODIFIED Requirements

### Requirement: ControllerState is a terminal concrete state, not a base class
`ControllerState` SHALL be a complete, concrete, final state extending `ConnectState<PDN>`. It SHALL NOT be designed as a base class for subclassing. No game-specific concrete states SHALL extend it. It SHALL handle all FDN experiences generically through the command protocol. `isPrimaryRequired()` and `isAuxRequired()` SHALL both return `true`.

#### Scenario: New FDN game experience is added
- **WHEN** a new FDN game experience is built
- **THEN** zero new PDN state classes are created; the new experience is implemented entirely in FDN code

#### Scenario: ControllerState handles all apps
- **WHEN** `ControllerState` is active and the FDN sends commands for any registered app
- **THEN** `ControllerState` handles them via the command protocol — `GAMEPAD_CONFIG`-driven button interactions, `GAME_EVENT` rendering, `SELECTION_CONFIRMED` sending — without app-specific logic

### Requirement: Lifecycle is owned entirely by ControllerState
`ControllerState` SHALL seal `onStateMounted`, `onStateLoop`, and `onStateDismounted` as `final`. There are no protected lifecycle hooks for subclasses — `ControllerState` is the terminal implementation.

#### Scenario: ControllerState mounts
- **WHEN** `ControllerState` mounts
- **THEN** it registers button callbacks, initializes the command registry, and prepares to receive FDN commands

### Requirement: Button behavior is driven by GamepadConfig, not hardcoded
`ControllerState` SHALL NOT register any button callbacks on mount. Button callbacks SHALL only be registered after a `GAMEPAD_CONFIG` command is received and `applyInputConfig` is called. On dismount, all registered button callbacks SHALL be removed regardless of which config was active.

#### Scenario: ControllerState mounts with no config
- **WHEN** `ControllerState` mounts before any `GAMEPAD_CONFIG` has been received
- **THEN** no button callbacks are registered; button presses produce no behavior until a `GAMEPAD_CONFIG` arrives

#### Scenario: Buttons are deregistered on dismount
- **WHEN** `ControllerState` dismounts
- **THEN** all button callbacks registered by the active config are removed and no dangling registrations remain

### Requirement: Disconnect detection is owned by ControllerState
`ControllerState` SHALL poll `isPersistentlyDisconnected()` in its state loop. If the FDN connection is lost, it SHALL set a transition flag that drives an app-level jump back to `Quickdraw`.

#### Scenario: FDN disconnects during ControllerState
- **WHEN** `isPersistentlyDisconnected()` returns true during the state loop
- **THEN** `ControllerState` sets `transitionToQuickdrawState = true` and the app jumps to `Quickdraw`

### Requirement: Standalone COMMAND_SET updates the command lookup table
`ControllerState` SHALL handle standalone `COMMAND_SET` commands by replacing its active `CommandSet`. When `cycleConfirmConfig=true` is the active mode, the selection index SHALL reset to 0 and the first option's label SHALL be rendered. When `cycleConfirmConfig=false` is the active mode, the command lookup table is updated silently — no index or display change occurs, since there is no cycling UI.

#### Scenario: FDN sends standalone COMMAND_SET in cycle-confirm mode
- **WHEN** the FDN sends a `COMMAND_SET` command while `cycleConfirmConfig=true` is active
- **THEN** the active command set is replaced, selection index resets to 0, and the first option's label is rendered

#### Scenario: FDN sends standalone COMMAND_SET in custom binding mode
- **WHEN** the FDN sends a `COMMAND_SET` command while `cycleConfirmConfig=false` is active
- **THEN** the active command set is replaced silently; button bindings are unchanged; no display update occurs

### Requirement: GAME_EVENT is rendered by ControllerState without game-specific logic
`ControllerState` SHALL handle `GAME_EVENT` by rendering the `GameEventPayload` directly — displaying `text` (if non-null) and looking up and rendering `icon` (if non-null). `ControllerState` SHALL NOT contain any game-specific interpretation of `GAME_EVENT` payloads.

#### Scenario: FDN sends GAME_EVENT with text
- **WHEN** `ControllerState` receives a `GAME_EVENT` with a non-null `text` field
- **THEN** it renders the text string on the PDN display; the PDN owns wrapping and layout

#### Scenario: FDN sends GAME_EVENT with icon
- **WHEN** `ControllerState` receives a `GAME_EVENT` with a non-null `icon` field
- **THEN** it looks up the `ResourceId` in the PDN's local resource table and renders the asset

### Requirement: GAMEPAD_CONFIG atomically reconfigures button bindings
When `ControllerState` receives a `GAMEPAD_CONFIG` command, it SHALL deregister all current button callbacks, replace its active `CommandSet` with the one in the config, wire new button callbacks per the `InputConfig`, and render an instruction screen if `showInstructions=true`. This transition SHALL be atomic — no old and new bindings shall be simultaneously active.

When `InputConfig.cycleConfirmConfig=true`:
- `PRIMARY_BUTTON` + `PRESS` → internal `cycleSelection()` (advances selection index, renders new label)
- `SECONDARY_BUTTON` + `PRESS` → internal `confirmSelection()` (sends `SELECTION_CONFIRMED` with current `CommandId`)
- Both `buttonPress` fields on the `ButtonConfig` structs are ignored
- All other interaction fields (`buttonDoubleClick`, `buttonLongPress`, `buttonRelease`) on both buttons are wired if non-`NONE`

When `InputConfig.cycleConfirmConfig=false`:
- All eight `ButtonConfig` fields are wired as direct command bindings if non-`NONE`
- Each fires `SELECTION_CONFIRMED(commandId)` when the interaction occurs

`CommandId::NONE` bindings are silently skipped — no callback is registered.

#### Scenario: GAMEPAD_CONFIG received with cycleConfirmConfig=true and a long-press binding
- **WHEN** `ControllerState` receives a config with `cycleConfirmConfig=true` and `secondaryButtonConfig.buttonLongPress = CMD_SPECIAL`
- **THEN** secondary PRESS confirms selection; secondary LONG_PRESS sends `SELECTION_CONFIRMED(CMD_SPECIAL)`; all old callbacks have been removed before new ones are registered

#### Scenario: GAMEPAD_CONFIG received with cycleConfirmConfig=false
- **WHEN** `ControllerState` receives a config with `cycleConfirmConfig=false` and direct command bindings
- **THEN** each non-NONE field is registered as a callback that fires `SELECTION_CONFIRMED(commandId)` on that interaction; no cycling behavior is active

#### Scenario: GAMEPAD_CONFIG received mid-session
- **WHEN** a new `GAMEPAD_CONFIG` arrives while `ControllerState` is already configured
- **THEN** all previous button callbacks are deregistered before new ones are applied; no transition period with mixed old/new callbacks

### Requirement: LAUNCH_APP updates ControllerState's app context
`ControllerState` SHALL handle `LAUNCH_APP` by storing the received `AppId` as its current app context. It MAY update a title or context display on receipt. It SHALL NOT transition to a different state on `LAUNCH_APP` — there is only one game state in the Controller app.

#### Scenario: FDN sends LAUNCH_APP while ControllerState is active
- **WHEN** `ControllerState` receives a `LAUNCH_APP` command
- **THEN** it stores the `AppId` and MAY update a display element; it does not transition to another state
