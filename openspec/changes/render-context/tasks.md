## 1. Core Type Definitions (lib/core)

- [ ] 1.1 Define `DisplayModule` struct (`text?: string`, `icon?: ResourceId`) in `lib/core/include/device/render/`
- [ ] 1.2 Define `LedOverlay` struct (`target`, `LEDColor colour`, `uint8_t brightness`, `uint32_t? ttlMs`)
- [ ] 1.3 Define `LedModule` struct (base layer variant: `AnimationDescriptor | LEDState`, `LedOverlay[] overlays`)
- [ ] 1.4 Define `AnimationDescriptor` struct (`IAnimation*`, `AnimationConfig`)
- [ ] 1.5 Define `HapticModule` struct (placeholder — empty, v1 no-op)
- [ ] 1.6 Add `Callback` type alias (`struct Callback { parameterizedCallbackFunction fn; void* ctx; }`)
- [ ] 1.7 Update `ButtonConfig` to add nullable `Callback callback` field alongside existing `CommandId commandId`
- [ ] 1.8 Move `CommandSet` from `GamepadConfig` top-level into `InputConfig`
- [ ] 1.9 Define `InterfaceFrame` struct with nullable modules (`DisplayModule?`, `LedModule?`, `InputConfig?`, `HapticModule?`)
- [ ] 1.10 Define `CompletionPolicy` enum class (`RETURN_TO_PRIOR`, `HOLD_LAST_FRAME`)
- [ ] 1.11 Define `FrameSequence` struct (`InterfaceFrame[] frames`, `CompletionPolicy policy`)

## 2. State Base Class Updates (lib/core)

- [ ] 2.1 Add `InterfaceFrame pendingDelta` member to `State` base class (all-null by default)
- [ ] 2.2 Add `bool frameDirty` member to `State` base class (default `false`)
- [ ] 2.3 Implement `void applyRenderState(InterfaceFrame delta)` — merges non-null modules into `pendingDelta`, sets `frameDirty = true`
- [ ] 2.4 Add `virtual InterfaceFrame getCurrentFrame() const` to `State` returning `pendingDelta`
- [ ] 2.5 Override `getCurrentFrame()` in `StateMachine` to delegate to `currentState->getCurrentFrame()`

## 3. StateMachine Updates (lib/core)

- [ ] 3.1 Add `RenderEngine*` field to `StateMachine`; inject via constructor or setter
- [ ] 3.2 Call `renderEngine->tick()` at the start of every `onStateLoop` iteration
- [ ] 3.3 After `currentState->onStateLoop()`, check `frameDirty`; call `renderEngine->apply(pendingDelta)`, reset `pendingDelta` to all-null, and reset `frameDirty = false`
- [ ] 3.4 Call `renderEngine->clearFrame()` in `commitState()` between dismount and mount

## 4. Abstract RenderEngine Base (lib/core)

- [ ] 4.1 Define `RenderEngine` abstract class in `lib/core/include/device/render/render-engine.hpp`; add `InterfaceFrame activeFrame` member (canonical applied state)
- [ ] 4.2 Implement `apply(const InterfaceFrame& delta)` — skip null modules, call protected virtual methods for non-null modules, merge non-null modules into `activeFrame` after apply, atomic button deregistration before `applyInputConfig`
- [ ] 4.3 Implement `tick()` — advance active animation, expire overlays, composite, write to light driver via `applyLeds`
- [ ] 4.4 Implement `clearFrame()` — deregister all callbacks, clear display, stop animation, clear overlays
- [ ] 4.5 Declare protected pure virtual `applyDisplay(const DisplayModule&) = 0`
- [ ] 4.6 Declare protected pure virtual `applyLeds(const LedModule&) = 0`
- [ ] 4.7 Declare protected pure virtual `applyHaptics(const HapticModule&) = 0`
- [ ] 4.8 Implement `addOverlay(LedOverlay)` — adds overlay to active `LedModule` without full frame re-apply
- [ ] 4.9 Implement `applyInputConfig(const InputConfig&)` in base — deregister all button callbacks, then iterate `ButtonConfig` fields and call `button->setButtonPress(cb.fn, cb.ctx, interaction)` for non-null callbacks

## 5. PdnRenderEngine (lib/esp32-drivers or src/pdn)

- [ ] 5.1 Implement `PdnRenderEngine : RenderEngine`
- [ ] 5.2 Implement `applyDisplay(DisplayModule&)` — calls PDN display driver with text and icon
- [ ] 5.3 Implement `applyLeds(LedModule&)` — initialises animation via `LightManager` or writes static `LEDState`
- [ ] 5.4 Implement `applyHaptics(HapticModule&)` — no-op in v1
- [ ] 5.5 Instantiate `PdnRenderEngine` in PDN `main.cpp`; inject into `StateMachine`

## 6. FdnRenderEngine (lib/esp32-drivers or src/fdn)

- [ ] 6.1 Implement `FdnRenderEngine : RenderEngine`
- [ ] 6.2 Implement `applyLeds(LedModule&)` — maps to FDN light driver
- [ ] 6.3 Implement `applyDisplay(DisplayModule&)` — no-op if FDN has no display driver
- [ ] 6.4 Implement `applyHaptics(HapticModule&)` — no-op in v1
- [ ] 6.5 Instantiate `FdnRenderEngine` in FDN `main.cpp`; inject into FDN state machine

## 7. LED Compositor (RenderEngine base)

- [ ] 7.1 Implement overlay expiry check in `tick()` — compare `millis() - overlayAddedAtMs` against `ttlMs`; remove expired overlays
- [ ] 7.2 Implement base-frame generation in `tick()` — call `animation->animate()` or use static `LEDState`
- [ ] 7.3 Implement overlay compositing in `tick()` — overwrite targeted LED values in base frame
- [ ] 7.4 Write final composited `LEDState` to `LightStrip` via `applyLeds` or direct driver call in `tick()`

## 8. McEventState — FrameSequence Processor

- [ ] 8.1 Implement `McEventState` (or `McEventStateMachine`) on PDN accepting a `FrameSequence`
- [ ] 8.2 On mount: populate callbacks on `frames[0].input` if present; call `renderEngine->apply(frames[0])`
- [ ] 8.3 Implement advancement: on command received or navigation callback, increment index, populate callbacks on next frame, call `renderEngine->apply(frames[i])`
- [ ] 8.4 On last frame completion: evaluate `CompletionPolicy`; transition accordingly
- [ ] 8.5 Connect `McEventState` to the `kMcEvent` preemption queue dispatch in PDN device layer

## 9. GamepadConfig Migration

- [ ] 9.1 Rename `GAMEPAD_CONFIG` command → `INTERFACE_FRAME` in `controller-command-types.hpp`
- [ ] 9.2 Rename `FdnGameState::setGamepadConfig()` → `setRenderFrame(InterfaceFrame)`
- [ ] 9.3 Update all `FdnGameState` concrete subclasses to call `setRenderFrame(InterfaceFrame { .input = ... })`
- [ ] 9.4 Update PDN `ControllerState` `INTERFACE_FRAME` handler to call `renderEngine->apply()` with the received frame after state populates callbacks
- [ ] 9.5 Remove `GamepadConfig` type; confirm no remaining references

## 10. PDN State Migration (one state at a time)

- [ ] 10.1 Migrate `Idle` state — replace direct driver calls with `applyRenderState` calls; remove `displayIsDirty` flag; convert `renderStats(PDN*)` to `updateStatsDisplay()` (no PDN parameter)
- [ ] 10.2 Migrate `Duel` / `DuelCountdown` states
- [ ] 10.3 Migrate `SymbolState` / `SymbolMatchedState`
- [ ] 10.4 Migrate `ControllerState`
- [ ] 10.5 Migrate remaining PDN states
- [ ] 10.6 Verify all `getPrimaryButton()->setButtonPress()`, `removeButtonCallbacks()`, `getLightManager()`, `getDisplay()` direct calls are removed from migrated states

## 11. FDN State Migration

- [ ] 11.1 Update `FdnGameState` base — replace `setGamepadConfig` pattern with `activeFrame`; call `renderEngine->apply` on mount
- [ ] 11.2 Migrate concrete FDN game states to populate `activeFrame.leds` and `activeFrame.display` where applicable
