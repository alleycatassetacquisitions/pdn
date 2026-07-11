## Why

Rendering logic is scattered across every state in the codebase. `onStateMounted` configures LED animations and wires button callbacks. `onStateLoop` calls display methods directly and manages dirty flags. `onStateDismounted` resets display state and clears callbacks. Every state reimplements the same teardown pattern. There is no single object that describes what a device should look like and how it should behave at a given moment — that description is implicit in hundreds of imperative driver calls spread across the state machine.

This change introduces `InterfaceFrame` as that object: a composable, serialisable snapshot of a device's complete peripheral and input state. A `RenderEngine` receives an `InterfaceFrame` and maps it to drivers. States produce frames instead of calling drivers directly. The same frame type travels over the network (ESP-NOW / MQTT), removing the boundary between "what we push from MC" and "what the device renders locally."

## What Changes

- **`InterfaceFrame`** replaces `GamepadConfig` as the canonical object describing a device's display, LED, input, and haptic state at a point in time
- **`FrameSequence`** is the transmission envelope — an ordered list of `InterfaceFrame` objects with a `CompletionPolicy`, enabling paginated multi-screen experiences with per-frame peripheral state
- **`RenderEngine`** (abstract base + device-specific implementations) receives a single `InterfaceFrame` and applies its modules to the device's drivers; owns button deregistration on frame swap
- **`State` base class** gains an `activeFrame` member and a `frameDirty` flag; lifecycle methods stay `void` and set the frame rather than calling drivers directly; `StateMachine` delegates `getCurrentFrame()` to the current state and calls the render engine on dirty transitions
- **LED layered compositing** in `LedModule`: a base animation layer plus optional time-limited overlays, composited by the render engine on each animation tick
- **`ButtonConfig`** gains a nullable `callback` field alongside `commandId`; the state builds callbacks (capturing whatever context it needs) before handing the frame to the render engine; the render engine wires callbacks to button drivers
- **`CommandSet`** moves from `GamepadConfig` top-level into `InputConfig`
- **BREAKING**: `GamepadConfig` is deprecated and replaced by `InterfaceFrame` + `InputConfig`
- **BREAKING**: Direct driver calls (`getDisplay()`, `getLightManager()`, `getPrimaryButton()`) in state lifecycle methods are replaced by frame mutation

## Capabilities

### New Capabilities

- `interface-frame`: `InterfaceFrame` type — `DisplayModule?`, `LedModule?`, `InputConfig?`, `HapticModule?` (future); `ButtonConfig` with `CommandId commandId` + nullable `Callback callback`; `CommandSet` inside `InputConfig`; all modules nullable for partial updates; unknown modules silently ignored
- `frame-sequence`: `FrameSequence` — ordered `InterfaceFrame[]` + `CompletionPolicy` (RETURN_TO_PRIOR | HOLD_LAST_FRAME); consuming state owns advancement and current index; no default navigation behaviour
- `render-engine`: Abstract `RenderEngine` base in `lib/core` with `apply(InterfaceFrame)`, `tick()`, `clearFrame()`; atomic button deregistration on frame swap; protected virtual `applyDisplay`, `applyLeds`, `applyHaptics`; `PdnRenderEngine` and `FdnRenderEngine` concrete implementations
- `state-render-context`: `State` base gains `activeFrame: InterfaceFrame` and `frameDirty: bool`; `StateMachine::getCurrentFrame()` delegates to current state; device loop calls `renderEngine->tick()` every iteration and `renderEngine->apply()` when `frameDirty`; `clearFrame()` called on state transition before mount
- `led-compositor`: `LedModule` carries a base layer (`AnimationDescriptor` or static `LEDState`) and optional `LedOverlay[]`; render engine composites base + overlays on each tick; overlays carry optional TTL and are auto-removed on expiry

### Modified Capabilities

- `gamepad-config`: `ButtonConfig` gains nullable `Callback callback` field; `CommandSet` moves into `InputConfig`; `GamepadConfig` deprecated — replaced by `InterfaceFrame` + `InputConfig`

## Impact

- **`lib/core`**: New `InterfaceFrame`, `FrameSequence`, `RenderEngine` base, `LedModule`/`LedOverlay` types; `State` base updated; `StateMachine` updated
- **`lib/esp32-drivers`**: `PdnRenderEngine` and `FdnRenderEngine` implementations
- **`src/pdn/`**: All PDN states migrated from direct driver calls to `activeFrame` mutation; `McEventState` added for FrameSequence processing
- **`src/fdn/`**: `FdnGameState` base updated; FDN states migrated
- **`fdn-pdn-controller-architecture` change**: `GamepadConfig` → `InterfaceFrame` migration; `GAMEPAD_CONFIG` command renamed `INTERFACE_FRAME`; `ButtonConfig` updated
- **`mqtt-integration` change**: `McEventPayload` carries `FrameSequence` instead of bare display data
