## ADDED Requirements

### Requirement: State base class exposes applyRenderState(InterfaceFrame) as the single render API
The `State` base class SHALL provide `void applyRenderState(InterfaceFrame delta)`. This is the only method states SHALL use to describe their render intent. It merges non-null modules from `delta` into the state's `pendingDelta` and sets `frameDirty = true`. States SHALL NOT call device drivers directly for display, LED, input, or haptic concerns.

#### Scenario: State describes full render intent on mount
- **WHEN** `onStateMounted` calls `applyRenderState({ .display = ..., .leds = ..., .input = ... })`
- **THEN** all three modules are merged into `pendingDelta`, `frameDirty` is set, and on the next tick the state machine applies all three to the render engine

#### Scenario: State updates only display mid-loop
- **WHEN** `onStateLoop` calls `applyRenderState({ .display = newStats })` with null `leds` and `input`
- **THEN** only `pendingDelta.display` is updated; the active LED animation and button bindings are not touched

### Requirement: applyRenderState merges successive calls within the same tick
When `applyRenderState` is called more than once in the same tick, each call's non-null modules SHALL be merged into `pendingDelta` additively. A later call SHALL overwrite an earlier call's module only if the later call provides a non-null value for that module.

#### Scenario: Two applyRenderState calls in the same loop tick
- **WHEN** `onStateLoop` calls `applyRenderState({ .display = newDisplay })` followed by `applyRenderState({ .leds = newLeds })` in the same tick
- **THEN** `pendingDelta` contains both `display` and `leds`; the state machine applies both in a single render engine call

### Requirement: Nullability of modules in the delta is the per-module dirty signal
`InterfaceFrame` modules SHALL NOT carry dirty flags. A non-null module in the `pendingDelta` means "apply this module." A null module means "do not touch this aspect of the current device state." No separate per-module dirty flags exist on `State` or `InterfaceFrame`.

#### Scenario: Null module is not applied
- **WHEN** `pendingDelta` has a non-null `DisplayModule` and a null `LedModule`
- **THEN** the render engine calls `applyDisplay()` and does not call `applyLeds()`; the running animation is unaffected

### Requirement: StateMachine drives apply on frameDirty and clears pendingDelta after apply
After calling `currentState->onStateLoop()`, `StateMachine` SHALL check `frameDirty`. If true, it SHALL call `renderEngine->apply(currentState->pendingDelta)`, reset `pendingDelta` to an all-null frame, and set `frameDirty = false`.

#### Scenario: StateMachine applies pending delta after loop
- **WHEN** a state calls `applyRenderState` during `onStateLoop`
- **THEN** the state machine calls `renderEngine->apply(pendingDelta)` at the end of that tick, clears `pendingDelta`, and resets `frameDirty`

#### Scenario: StateMachine does not call apply when frame is not dirty
- **WHEN** `onStateLoop` completes without calling `applyRenderState`
- **THEN** `frameDirty` is false; the state machine does not call `renderEngine->apply()`; no driver writes occur for that tick

### Requirement: StateMachine calls renderEngine->tick() every loop iteration unconditionally
`StateMachine` SHALL call `renderEngine->tick()` on every loop iteration before checking `frameDirty`. This drives the LED animation compositor independently of any state changes.

#### Scenario: Animation advances every tick regardless of state changes
- **WHEN** `onStateLoop` completes without calling `applyRenderState`
- **THEN** `renderEngine->tick()` still advances the active LED animation and writes the next frame to the light driver

### Requirement: RenderEngine owns the canonical activeFrame — State owns only pendingDelta
The render engine SHALL maintain its own `activeFrame` representing the last successfully applied state of all modules. After `apply(delta)` completes, the render engine SHALL merge `delta`'s non-null modules into its `activeFrame`. States have no reference to the render engine's `activeFrame`.

#### Scenario: Render engine tracks applied state across multiple partial updates
- **WHEN** three successive ticks apply display-only, leds-only, and input-only deltas respectively
- **THEN** the render engine's `activeFrame` contains the most recently applied value for each module after each tick

### Requirement: StateMachine calls renderEngine->clearFrame() on commitState() and forces full apply on mount
On `commitState()`, `StateMachine` SHALL call `renderEngine->clearFrame()` after dismounting the old state and before mounting the new state. On `onStateMounted()`, the state machine SHALL treat the incoming state's first `applyRenderState` call as a full apply — no special handling needed since `pendingDelta` starts empty and `clearFrame()` has wiped the render engine's `activeFrame`.

#### Scenario: State transition clears all prior render state
- **WHEN** a state transition is committed
- **THEN** `clearFrame()` deregisters all button callbacks, clears display, and stops animation before the incoming state's `onStateMounted` runs

### Requirement: StateMachine::getCurrentFrame() delegates to the current state recursively
`StateMachine` SHALL override `getCurrentFrame()` to return `currentState->getCurrentFrame()`. For nested state machines, delegation chains until a leaf `State` is reached.

#### Scenario: Nested StateMachine returns leaf state's pending frame
- **WHEN** `getCurrentFrame()` is called on a `StateMachine` whose current state is itself a `StateMachine`
- **THEN** delegation recurses until a leaf `State` is reached

### Requirement: States that process FrameSequences call renderEngine->apply() directly
States that own `FrameSequence` playback (e.g. `McEventState`) SHALL call `renderEngine->apply(frames[currentIndex])` directly when advancing rather than using `applyRenderState`. Direct apply bypasses `pendingDelta` and fires immediately.

#### Scenario: McEventState advances to next frame
- **WHEN** `McEventState` advances its current index
- **THEN** it calls `renderEngine->apply(sequence.frames[currentIndex])` immediately; `pendingDelta` on the state is not used for sequence frames
