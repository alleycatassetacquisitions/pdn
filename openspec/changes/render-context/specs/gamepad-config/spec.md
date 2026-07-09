## MODIFIED Requirements

### Requirement: GamepadConfig is deprecated — replaced by InterfaceFrame and InputConfig
`GamepadConfig` SHALL be removed. All usage sites SHALL be migrated to `InterfaceFrame`. The `GAMEPAD_CONFIG` command on `kFdnCommand` SHALL be renamed `INTERFACE_FRAME`. The FDN's `setGamepadConfig()` helper on `FdnGameState` SHALL be renamed `setRenderFrame()` and SHALL accept an `InterfaceFrame`.

**Reason:** `InterfaceFrame` is a superset of `GamepadConfig` that also carries display and LED state. Maintaining both types creates two overlapping abstractions. All FDN experiences now describe their full device state in a single `InterfaceFrame`.

**Migration:** Replace all `GamepadConfig` construction sites with `InterfaceFrame { .input = InputConfig { ... } }`. Replace `GAMEPAD_CONFIG` command dispatch with `INTERFACE_FRAME`. Replace `setGamepadConfig(GamepadConfig)` calls with `setRenderFrame(InterfaceFrame)`.

#### Scenario: FDN sends an InterfaceFrame to PDN
- **WHEN** an `FdnGameState` calls `setRenderFrame(InterfaceFrame)` on mount
- **THEN** the FDN broadcasts an `INTERFACE_FRAME` command on `kFdnCommand` carrying the full `InterfaceFrame`; the PDN's consuming state applies it via the render engine

## MODIFIED Requirements

### Requirement: CommandSet is a member of InputConfig
`CommandSet` SHALL be a member of `InputConfig`, not a top-level field on `InterfaceFrame` or `GamepadConfig`. All existing references to a top-level `commandSet` field SHALL be updated to reference `inputConfig.commandSet`.

**Reason:** `CommandSet` defines the command vocabulary that `ButtonConfig` bindings reference. Co-locating them in `InputConfig` makes the dependency explicit and prevents a `CommandSet` from existing without its binding context.

#### Scenario: InputConfig with CommandSet applied
- **WHEN** an `InterfaceFrame` with `InputConfig { commandSet: [ATTACK, DEFEND], ... }` is applied
- **THEN** the render engine wires button callbacks using the command IDs defined in `inputConfig.commandSet`

## ADDED Requirements

### Requirement: ButtonConfig carries a nullable Callback field for runtime wiring
`ButtonConfig` SHALL add a `Callback callback` field (nullable, raw function pointer + `void* ctx`). `callback` SHALL always be null after deserialisation. The consuming state SHALL populate `callback` by constructing a captureless lambda using the `void* ctx` pattern before passing the frame to the render engine. The render engine SHALL wire non-null callbacks to the button driver and silently skip null ones.

#### Scenario: State populates callback before apply
- **WHEN** a state constructs a `ButtonConfig` with `commandId = CommandId::ATTACK` and a populated `callback`
- **THEN** the render engine calls `button->setButtonPress(callback.fn, callback.ctx, CLICK)` on apply

#### Scenario: Deserialised ButtonConfig has null callback — render engine skips wiring
- **WHEN** a `ButtonConfig` received over the network has a null `callback`
- **THEN** the render engine does not register any callback for that button interaction
