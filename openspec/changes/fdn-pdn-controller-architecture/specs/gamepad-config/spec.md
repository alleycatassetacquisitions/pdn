## ADDED Requirements

### Requirement: GamepadConfig is a composable FDN-sent configuration object
`GamepadConfig` SHALL be defined in `lib/core` and SHALL contain: a `CommandSet` (always present), an `InputConfig` (always present), and a `bool showInstructions`. It SHALL be designed for future extension — `LedConfig` and `HapticConfig` are anticipated future fields. Adding a future module SHALL NOT break existing configs that omit it.

#### Scenario: FDN sends a GamepadConfig
- **WHEN** the FDN sends `GAMEPAD_CONFIG` on `kFdnCommand`
- **THEN** the PDN receives a `GamepadConfig` containing a `CommandSet`, an `InputConfig`, and a `showInstructions` flag

### Requirement: CommandSet is embedded in GamepadConfig
Every `GamepadConfig` SHALL include a `CommandSet`. A `CommandSet` SHALL be an ordered list of `Command` objects, each with `CommandId id`, `string label`, and `ResourceId icon`. The `CommandSet` in the config defines all commands that bindings may reference.

#### Scenario: GamepadConfig arrives with command definitions
- **WHEN** `ControllerState` receives a `GAMEPAD_CONFIG`
- **THEN** it replaces its active `CommandSet` with the one from the config; all subsequent binding dispatches and instruction rendering use this set

### Requirement: CommandId::NONE is the null sentinel for unbound slots
`CommandId` SHALL include a reserved `NONE` value (e.g. `= 0`). A `ButtonConfig` field set to `CommandId::NONE` means that interaction is unbound. Unbound interactions SHALL be silently ignored — no callback is registered and no command is sent.

#### Scenario: Unbound interaction fires on device
- **WHEN** a player performs a button interaction mapped to `CommandId::NONE`
- **THEN** nothing happens; no command is sent to the FDN and no error is logged

### Requirement: InputConfig defines button bindings via a flat per-button struct
`InputConfig` SHALL contain a `bool cycleConfirmConfig`, a `ButtonConfig primaryButtonConfig` (top button = `PRIMARY_BUTTON`), and a `ButtonConfig secondaryButtonConfig` (bottom button = `SECONDARY_BUTTON`). `ButtonConfig` SHALL contain four nullable `CommandId` fields: `buttonPress`, `buttonDoubleClick`, `buttonLongPress`, and `buttonRelease`.

#### Scenario: InputConfig fields map to button interactions
- **WHEN** `ControllerState` applies an `InputConfig`
- **THEN** each non-NONE field is registered as a button callback on the corresponding button and interaction type

### Requirement: cycleConfirmConfig overrides PRESS on both buttons
When `cycleConfirmConfig=true`, `PRIMARY_BUTTON` + `PRESS` SHALL perform the internal cycle-selection action and `SECONDARY_BUTTON` + `PRESS` SHALL perform the internal confirm-selection action (sending `SELECTION_CONFIRMED` with the currently selected `CommandId`). The `buttonPress` fields on both `ButtonConfig` structs SHALL be ignored. All other interactions (doubleClick, longPress, release) remain freely bindable regardless of the flag.

#### Scenario: cycleConfirmConfig=true with additional bindings
- **WHEN** `cycleConfirmConfig=true` and `secondaryButtonConfig.buttonLongPress = CMD_SPECIAL`
- **THEN** secondary PRESS → confirm selection; secondary LONG_PRESS → sends `SELECTION_CONFIRMED(CMD_SPECIAL)` to FDN; both behaviors are active simultaneously

#### Scenario: cycleConfirmConfig=false
- **WHEN** `cycleConfirmConfig=false`
- **THEN** all eight interaction slots are treated as direct command bindings; no cycling or confirm behavior is active unless explicitly bound via a future system action mechanism

### Requirement: LONG_PRESS_DURATION_MS is a universal constant in lib/core
`LONG_PRESS_DURATION_MS` SHALL be defined as a `constexpr uint32_t` in `lib/core`. Both PDN and FDN firmware SHALL use this constant. It SHALL NOT be configurable per `GamepadConfig` in v1.

#### Scenario: Long press threshold is consistent across devices
- **WHEN** any PDN or FDN device evaluates a long press interaction
- **THEN** it uses the same `LONG_PRESS_DURATION_MS` value from `lib/core`

### Requirement: showInstructions triggers an auto-generated instruction screen
When `showInstructions=true`, `ControllerState` SHALL render an instruction screen after applying the new config. Instruction content SHALL be generated from the config without additional payload: for each active non-cycle binding, the screen shows the interaction type name, the command's label, and the command's icon. For cycle/confirm interactions (when `cycleConfirmConfig=true`), fixed standard descriptions are shown.

#### Scenario: Custom config with showInstructions=true
- **WHEN** `ControllerState` receives a `GAMEPAD_CONFIG` with `showInstructions=true` and direct command bindings
- **THEN** it renders an instruction screen showing e.g. "Press → [SWORD] ATTACK", "Double Click → [SHIELD] DEFEND", "Long Press → [CROSS] HEAL"

#### Scenario: showInstructions=false
- **WHEN** `ControllerState` receives a `GAMEPAD_CONFIG` with `showInstructions=false`
- **THEN** no instruction screen is shown; the config is applied silently

### Requirement: ControllerState atomically replaces config on GAMEPAD_CONFIG receipt
When a new `GAMEPAD_CONFIG` arrives, `ControllerState` SHALL deregister all current button callbacks before registering new ones. The transition SHALL be atomic from the state's perspective — no period where old and new bindings are simultaneously active.

#### Scenario: FDN sends a new config mid-session
- **WHEN** the FDN sends `GAMEPAD_CONFIG` while `ControllerState` is active with an existing config
- **THEN** all old button callbacks are removed, the new config is applied, and if `showInstructions=true` the instruction screen is rendered

### Requirement: Standalone COMMAND_SET updates remain available
The FDN MAY send a standalone `COMMAND_SET` command to update the active command list without changing button bindings. `ControllerState` SHALL apply the new `CommandSet` to its active set without rewiring button callbacks.

#### Scenario: FDN updates command labels mid-state
- **WHEN** the FDN sends a standalone `COMMAND_SET` (not a full `GAMEPAD_CONFIG`)
- **THEN** the active command set is replaced; button bindings are unchanged
