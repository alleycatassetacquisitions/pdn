## ADDED Requirements

### Requirement: RenderEngine is an abstract base in lib/core with device-specific concrete implementations
`RenderEngine` SHALL be defined as an abstract class in `lib/core`. It SHALL provide public non-virtual `apply(const InterfaceFrame&)`, `tick()`, and `clearFrame()` methods. Device-specific application logic SHALL live in protected pure virtual methods `applyDisplay(const DisplayModule&)`, `applyLeds(const LedModule&)`, and `applyHaptics(const HapticModule&)`. `PdnRenderEngine` and `FdnRenderEngine` SHALL provide concrete implementations.

#### Scenario: PDN render engine applies a full frame
- **WHEN** `PdnRenderEngine::apply()` is called with an `InterfaceFrame` containing all modules
- **THEN** it calls `applyDisplay`, `applyLeds`, and applies `InputConfig` wiring — all via its concrete driver implementations

#### Scenario: FDN render engine skips display module it does not support
- **WHEN** `FdnRenderEngine::apply()` is called with an `InterfaceFrame` containing a `DisplayModule`
- **THEN** `FdnRenderEngine::applyDisplay` is a no-op if the FDN has no display driver; no error is produced

### Requirement: apply() atomically deregisters existing button callbacks before applying new InputConfig
When `apply(InterfaceFrame)` is called with a non-null `InputConfig`, the render engine SHALL call `removeButtonCallbacks()` on all button drivers before registering any new callbacks from the incoming `ButtonConfig` fields. Old and new callbacks SHALL never be simultaneously active.

#### Scenario: New InputConfig replaces existing bindings
- **WHEN** a second `InterfaceFrame` with a different `InputConfig` is applied
- **THEN** all callbacks from the previous `InputConfig` are removed before the new ones are registered; no prior callback fires after the swap

### Requirement: apply() skips null modules without affecting current driver state
When `apply(InterfaceFrame)` is called with a null module, the render engine SHALL leave the corresponding driver state unchanged. It SHALL NOT clear or reset a driver whose module is null.

#### Scenario: Apply with null LedModule leaves animation running
- **WHEN** a frame with a non-null `DisplayModule` and a null `LedModule` is applied
- **THEN** the display is updated and the currently running LED animation continues without interruption

### Requirement: tick() drives the LED animation compositor on every device loop call
`tick()` SHALL be called by the device loop on every iteration regardless of `frameDirty`. It SHALL advance the active animation (if any), composite overlays, and write the resulting `LEDState` to the `LightStrip` driver.

#### Scenario: tick() advances animation each loop
- **WHEN** `tick()` is called with a looping animation active
- **THEN** the animation produces its next frame, overlays are composited, and the result is written to the light driver

### Requirement: clearFrame() deregisters all callbacks, clears display, and stops active animation
`clearFrame()` SHALL deregister all button callbacks, call the display driver's clear/invalidate method, and stop any running animation. It SHALL be called by `StateMachine` on `commitState()` before mounting a new state.

#### Scenario: State transition clears prior render state
- **WHEN** `StateMachine` transitions to a new state
- **THEN** `clearFrame()` is called before the new state's `onStateMounted`, ensuring no prior callbacks, display content, or animations bleed into the new state

### Requirement: RenderEngine is the only layer that calls button driver wiring methods
States and state machines SHALL NOT call `setButtonPress`, `setButtonDoubleClick`, `setButtonLongPress`, `setButtonRelease`, or `removeButtonCallbacks` directly. All button driver interaction SHALL go through the render engine.

#### Scenario: Migrated state registers input via InterfaceFrame
- **WHEN** a state needs to respond to a button press
- **THEN** it constructs a callback and places it in `ButtonConfig` on its `activeFrame`; it does not call the button driver directly
