## Context

Every state in the codebase currently calls device drivers directly in its lifecycle methods. `Idle::onStateMounted` creates animation objects and calls `pdn->getLightManager()->startAnimation(...)`. `Idle::onStateDismounted` calls `pdn->getDisplay()->setGlyphMode(...)` and `pdn->getPrimaryButton()->removeButtonCallbacks()`. `renderStats` calls the display driver imperatively. The `displayIsDirty` flag is a manually managed re-render guard.

This pattern works but has compounding costs: every state reimplements teardown, rendering knowledge is scattered rather than centralised, and there is no shared language between "what we render locally" and "what we receive over the network." `GamepadConfig` was a step toward a protocol object for input config, but it covers only one concern and doesn't reach LEDs or display.

**Constraints:**
- C++17 embedded target — no RTTI, limited heap, no exceptions
- `State`/`TypedState`/`StateMachine` lifecycle must be preserved
- `IAnimation` → `LEDState` pattern already established — build on it
- `parameterizedCallbackFunction` (raw function pointer + `void* ctx`) is the existing callback convention
- No smart pointers; raw `new`/`delete` ownership model

## Goals / Non-Goals

**Goals:**
- Single composable object (`InterfaceFrame`) describes complete device peripheral + input state
- States produce frames instead of calling drivers; lifecycle methods stay `void`
- `RenderEngine` is the only layer that touches drivers; owns callback deregistration
- `FrameSequence` enables multi-screen paginated experiences with per-frame state
- LED layered compositing: base animation + independent overlays
- Same `InterfaceFrame` type travels on the network and runs locally — no separate wire/runtime types except for the nullable `callback` field on `ButtonConfig`

**Non-Goals:**
- Replacing the animation system (`IAnimation`, `LightManager`) — the render engine wraps it
- Per-state custom render engines — `PdnRenderEngine` handles all PDN states
- Synchronous multi-device frame coordination
- Haptic module implementation (placeholder only)

## Decisions

### 1. InterfaceFrame replaces GamepadConfig

**Decision:** `InterfaceFrame` is a superset of `GamepadConfig`. It carries display, LED, input, and haptic modules — all nullable. `GamepadConfig` is deprecated. The `GAMEPAD_CONFIG` command on `kFdnCommand` is renamed `INTERFACE_FRAME`. `CommandSet` moves from `GamepadConfig` top-level into `InputConfig`.

**Rationale:** `GamepadConfig` covered input only. As soon as we needed to describe LED state or display content alongside input config in a single network message (the MC push use case), we needed a broader type. `InterfaceFrame` is that type. Keeping `GamepadConfig` as a separate type alongside it creates two overlapping abstractions with no clear boundary.

**Migration:** The `fdn-pdn-controller-architecture` change's `setGamepadConfig()` → `setRenderFrame()`. All `GamepadConfig` construction sites produce `InterfaceFrame` with an `InputConfig` and a null `LedModule`/`DisplayModule` until states are fully migrated.

### 2. All modules are nullable — partial updates are first-class

**Decision:** Each module on `InterfaceFrame` (`DisplayModule?`, `LedModule?`, `InputConfig?`, `HapticModule?`) is independently nullable. A null module means "do not change this aspect of the current state." The render engine merges incoming frames against the current active state per module.

**Rationale:** This enables lightweight updates (e.g. a display-only `GAME_EVENT` push without rewiring buttons) and enables MC to push a single-module overlay without describing the full device state. It also means most in-game state mutations are small (change one field on `activeFrame.display`, set `frameDirty`).

### 3. ButtonConfig carries CommandId (wire) + nullable Callback (runtime)

**Decision:** `ButtonConfig` has two fields: `CommandId commandId` and `Callback callback` (nullable). On the wire (ESP-NOW / MQTT), only `commandId` is serialised. Before a frame is handed to the render engine, the state populates `callback` by constructing a captureless lambda using the `void* ctx` pattern, encoding whatever behaviour it needs. The render engine wires non-null callbacks to the button driver and skips null ones.

**Rationale:** Keeps `InterfaceFrame` serialisable (commandId is an integer) while giving the render engine a purely mechanical wiring job (it doesn't need to know what commands mean). The state encodes all protocol knowledge (send `SELECTION_CONFIRMED`, advance a sequence, etc.) into the closure before the frame reaches the engine.

**CommandId stays:** Even in runtime context, `commandId` remains in `ButtonConfig`. It provides identity for upstream reporting (e.g., `McEventState` knowing the player selected ACCEPT) after the callback fires. The callback can use the commandId from its capture context rather than reading it back from the struct.

### 4. Render engine scope: apply a single frame to output drivers + deregistration

**Decision:** `RenderEngine::apply(InterfaceFrame)` applies each non-null module to its driver. For `InputConfig`, it deregisters all existing button callbacks first, then registers the non-null callbacks from the new frame's `ButtonConfig` fields. `RenderEngine::tick()` advances the active LED animation and composites overlays on each call. `RenderEngine::clearFrame()` deregisters all callbacks, clears display, and stops the active animation.

**Rationale:** The render engine is the only layer that touches drivers, so it is the right owner of deregistration. Atomic deregister-then-register on `apply()` preserves the invariant that old and new callbacks are never simultaneously active — the same guarantee currently provided by `ControllerState`'s explicit deregister-then-rewire pattern.

**What the render engine does NOT do:** advance through a `FrameSequence`, decide when to re-render, manage state transitions, interpret `commandId` values, or produce display content.

### 5. Abstract RenderEngine base; device-specific concrete implementations

**Decision:** `RenderEngine` in `lib/core` is an abstract base with `apply()`, `tick()`, and `clearFrame()` as public non-virtual methods (template method pattern). Protected virtual `applyDisplay(DisplayModule&)`, `applyLeds(LedModule&)`, and `applyHaptics(HapticModule&)` are overridden by `PdnRenderEngine` (in `lib/esp32-drivers` or `src/pdn`) and `FdnRenderEngine`. The compositing logic and dirty tracking in `apply()` are device-agnostic and live once in the base.

**Rationale:** Display, LED, and haptic driver interfaces are device-specific (PDN has a display; FDN may not). The base handles merging, null module skipping, and deregistration. Concrete classes provide the driver mapping. This avoids duplicating compositing logic per device while keeping the driver calls decoupled.

### 6. State exposes applyRenderState(InterfaceFrame); render engine owns canonical activeFrame

**Decision:** `State` base class exposes a single method `void applyRenderState(InterfaceFrame delta)`. Calling it merges non-null modules from `delta` into the state's `pendingDelta` and sets `frameDirty = true`. States only ever call `applyRenderState` — they never mutate frame fields directly and never call device drivers. The nullability of each module in the passed frame is the per-module dirty signal; no separate per-module flags exist. Multiple calls in the same tick merge additively into `pendingDelta`.

`StateMachine` checks `frameDirty` after each `loop()` call and calls `renderEngine->apply(pendingDelta)` when true, then clears `pendingDelta` and resets `frameDirty`. `renderEngine->tick()` is called every iteration unconditionally, driving LED animation independently of state changes.

The render engine owns its own `activeFrame` — the canonical record of what is currently applied to the device. After each `apply(delta)`, it merges non-null modules into `activeFrame`. States have no reference to this. On `commitState()`, `renderEngine->clearFrame()` wipes `activeFrame` and deregisters all callbacks before the new state mounts.

**Rationale:** A single `applyRenderState` call site is simpler to author and harder to misuse than per-module setters or direct field mutation. The delta merge model naturally handles partial updates — null module means "leave it alone." Successive calls in the same tick compose without overwriting. Animation ticking is cleanly separated from display/input updates because `tick()` runs independently of `frameDirty`. The render engine owning `activeFrame` keeps the authoritative render state in one place.

### 7. FrameSequence: consuming state owns advancement, CompletionPolicy on the envelope

**Decision:** `FrameSequence` is an ordered `InterfaceFrame[]` with a `CompletionPolicy` (RETURN_TO_PRIOR | HOLD_LAST_FRAME). The consuming state (e.g. `McEventState`) tracks `currentIndex`, calls `renderEngine->apply(frames[currentIndex])` on mount and on advancement, and checks completion policy when `currentIndex` reaches the last frame. The render engine has no knowledge of sequences.

**Rationale:** Sequence advancement logic belongs to the state — it may advance on a button press, on a timer, on a command received, or on any other condition. Encoding this in the render engine would couple a display concern to application logic. States already own this kind of conditional advancement (see `displayIsDirty`, match timers, etc.).

### 8. LED layered compositing: base layer + TTL overlays

**Decision:** `LedModule` carries a base layer (`AnimationDescriptor { IAnimation*, AnimationConfig }` or `LEDState` for static) and an optional `LedOverlay[]`. Each `LedOverlay` targets specific LED indices or a `LightIdentifier` zone, carries a colour/brightness, and an optional `ttlMs`. On each `tick()`, the render engine calls `animation->animate()` to produce a base `LEDState`, then composites overlays in order (overlay wins on targeted LEDs), then writes to `LightStrip`. Overlays with expired TTL are removed before compositing.

**Rationale:** The existing `IAnimation` → `LEDState` pattern already separates frame generation from driver write. The compositor is the thin new layer between them. This enables the transmit-light flash, MC easter egg colour burst, and other "individual LED fires while animation runs" use cases without any changes to animations or the `LightStrip` driver.

## Risks / Trade-offs

- **Migration scope is large** — every state in the codebase needs to be migrated from direct driver calls to `activeFrame` mutation. Mitigation: states can migrate incrementally; a state that still calls drivers directly remains functional since the render engine and direct driver calls can coexist during transition (render engine only applies what's in `activeFrame`).

- **`ButtonConfig` carries both wire and runtime fields** — the `callback` field is meaningless on the wire but present in the type. Mitigation: serialisation explicitly skips the `callback` field; it is always null on deserialise and populated by the state before reaching the render engine.

- **ESP-NOW 250-byte limit for full InterfaceFrame** — a frame with `DisplayModule`, `InputConfig` with full `CommandSet`, and `LedModule` may approach or exceed the single-packet limit. Mitigation: the existing multi-packet clustering support in `EspNowManager` handles oversized payloads; verify `kMcEvent` and `kFdnCommand` packet types are covered by clustering.

- **Nested StateMachine frame delegation** — `StateMachine::getCurrentFrame()` must delegate recursively to the leaf state. A `StateMachine` that is itself a state in another `StateMachine` must not return its own `activeFrame` but its inner `currentState`'s frame. Mitigation: `StateMachine` overrides `getCurrentFrame()` to delegate rather than return its own member.

- **frameDirty on animation-only updates** — a state that only runs an animation (set once on mount, never changes) never calls `applyRenderState` after mount, so `frameDirty` is never set again. `tick()` drives the animation independently every loop. No risk of spurious re-applies.

## Migration Plan

1. Define `InterfaceFrame`, `FrameSequence`, `DisplayModule`, `LedModule`, `LedOverlay`, `InputConfig` (updated), `ButtonConfig` (updated), `CompletionPolicy` in `lib/core`
2. Add `activeFrame: InterfaceFrame` and `frameDirty: bool` to `State` base class; add `getCurrentFrame()` virtual method
3. Update `StateMachine`: override `getCurrentFrame()` to delegate; call `renderEngine->apply()` on `frameDirty`; call `renderEngine->clearFrame()` on `commitState()`
4. Implement abstract `RenderEngine` base in `lib/core`
5. Implement `PdnRenderEngine` in `lib/esp32-drivers`
6. Implement `FdnRenderEngine` in `lib/esp32-drivers`
7. Implement LED compositor in `RenderEngine::tick()`
8. Implement `McEventState` on PDN for `FrameSequence` processing
9. Migrate PDN states from direct driver calls to `activeFrame` mutation (one state at a time)
10. Migrate FDN `FdnGameState` base to use `activeFrame`; update `setRenderFrame()` helper
11. Update `fdn-pdn-controller-architecture` change: `GamepadConfig` → `InterfaceFrame`, `ButtonConfig` callback field, `CommandSet` into `InputConfig`
12. Update `mqtt-integration` change: `McEventPayload` carries `FrameSequence`

## Open Questions

1. **`frameDirty` granularity** — should there be separate dirty flags per module (displayDirty, ledDirty, inputDirty) to avoid triggering unnecessary driver writes on partial updates? Or is a single `frameDirty` sufficient given the render engine skips null modules?

2. **LedOverlay blend mode** — is simple "overlay wins" sufficient, or do we need additive blending (add overlay colour to base) for effects like brightening specific LEDs?

3. **FrameSequence auto-advance timer** — should `FrameSequence` optionally carry a per-frame `autoAdvanceMs` for cinematic sequences that progress without user input? Out of scope for v1 but worth noting.

4. **McEventState scope** — does `McEventState` live in `lib/core` (device-agnostic sequence playback) or in `src/pdn` (PDN-specific)? FDN may also need to play sequences for its own display.
