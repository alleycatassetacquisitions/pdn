## Context

The `fdn-experience` branch introduced the first working PDN↔FDN controller session using ad-hoc packet types, per-state button registration, and manual disconnect logic. The original architecture proposed a base class / subclass model. Design exploration revealed a superior model: the PDN Controller app can be a **protocol terminal** — a fixed, complete implementation driven entirely by the FDN through the command protocol. No game-specific subclasses are needed on the PDN. This document captures both the original principled architecture and the terminal state refinement, along with the symmetric FDN game state architecture.

**Constraints:**
- C++17, embedded target — no RTTI, limited heap
- Existing `ConnectState<PDN>` / `ConnectState<FDN>` lifecycle must be preserved
- FDN-side base class is new work in this change
- Per-port MAC routing (`ControllerWirelessManager` rewrite) is deferred to the comms architecture rewrite
- Routing states on both sides are designed with planned deprecation in mind

## Goals / Non-Goals

**Goals:**
- Single shared command vocabulary in `lib/core` — one definition, both targets
- `ControllerState` as a terminal concrete state — complete for all FDN experiences, no subclasses
- PDN is a pure display terminal and input peripheral — all game logic and display decisions live on the FDN
- `FdnGameState` base class — symmetric to `ControllerState`, FDN-side
- Multi-PDN support (1 or 2 PDNs) with event-driven connect/disconnect
- Adding a new FDN game experience requires **zero new PDN code**

**Non-Goals:**
- More than 2 simultaneous PDNs (noted as future extension point)
- Daisy-chain forwarding (noted as extension point)
- Per-port MAC routing rewrite (deferred to comms rewrite)
- FDN experiences that require game-specific PDN rendering logic (not supported in terminal model)

## Decisions

### 1. Two unified channels replacing three ad-hoc packet types

**Decision:** Replace `kGameSelect` + `kControllerCommand` + `kGameResponse` with `kFdnCommand` (FDN→PDN) and `kPdnInput` (PDN→FDN). Command IDs and packet structs live in `lib/core/include/controller/controller-command-types.hpp`.

**Rationale:** The three old types were logically a single protocol. Unifying them under two directional channels makes the contract explicit, removes the echo-confirmation anti-pattern, and gives both sides a clean vocabulary to extend.

### 2. LAUNCH_APP replaces GAME_SELECT; AppId replaces GameId

**Decision:** `GAME_SELECT` is renamed `LAUNCH_APP` and its payload carries an `AppId` (not `GameId`). The app ID space includes games, the game selection menu, and any future applications the FDN can direct the PDN to run.

**Rationale:** "Game select" implied the FDN was routing between games. The broader concept is app launch — the FDN tells the PDN which application to run. The game selection menu is itself one such application. This naming reflects the actual generality.

### 3. GamepadConfig / SELECTION_CONFIRMED as the universal interaction model

**Decision:** Button behavior and the active command set on the PDN are driven by `GamepadConfig`, sent from the FDN via `GAMEPAD_CONFIG` on `kFdnCommand`. The PDN never sends raw button identifiers — every input event is `SELECTION_CONFIRMED(commandId)`, where `commandId` is either the currently selected command (in cycle mode) or a directly bound command ID (in custom mode).

`GamepadConfig` contains a `CommandSet` (all available commands with `id`, `label`, and `icon`) and an `InputConfig` (button interaction → command mappings). `InputConfig` supports two modes via `cycleConfirmConfig`:

- **`cycleConfirmConfig=true` (cycle-confirm mode):** `PRIMARY_BUTTON` PRESS internally advances the selection index through the `CommandSet`; `SECONDARY_BUTTON` PRESS sends `SELECTION_CONFIRMED` with the currently selected command. This preserves the classic cycle-and-confirm UX familiar from `SymbolState`.
- **`cycleConfirmConfig=false` (custom mode):** Each `ButtonConfig` field maps a specific button interaction (PRESS, DOUBLE_CLICK, LONG_PRESS, RELEASE — v1 supported set) directly to a `CommandId`. When that interaction fires, `SELECTION_CONFIRMED(commandId)` is sent.

Both modes are composable: a `cycleConfirmConfig=true` config can simultaneously bind non-PRESS interactions (e.g. `buttonLongPress`) to custom commands.

**Rationale:** The original hardcoded top=cycle/bottom=confirm model generalizes the `SymbolState` mechanic and works for simple experiences. `GamepadConfig` extends this to support richer input without requiring any PDN subclasses. The FDN drives all interaction variation through the protocol — the PDN's button wiring is reconfigurable at any time by sending a new `GAMEPAD_CONFIG`.

### 4. GameEventPayload carries pre-formatted display data

**Decision:** `GAME_EVENT` carries pre-formatted display data: `{ text?: string, icon?: ResourceId }`. Both fields are nullable. The FDN formats what the PDN displays. The PDN renders text and icon without game-specific knowledge. The PDN owns text wrapping and layout once it receives the string. ResourceId maps to a PDN-local asset; the PDN owns the resource-to-asset mapping.

**Rationale:** If the PDN is a pure display terminal, it should not contain game-specific display logic. Having the FDN pre-format display content keeps all game intelligence on the FDN and makes the `ControllerState` terminal model viable. The PDN's rendering capability (wrapping, layout, asset lookup) is stable and reusable across all experiences.

### 5. ControllerState is a terminal concrete state — no subclasses

**Decision:** `ControllerState` is a complete, final, concrete state. It is not a base class. No game-specific subclasses exist. It handles `COMMAND_SET` cycling, `GAME_EVENT` rendering, `SELECTION_CONFIRMED` sending, `LAUNCH_APP` context update, and disconnect detection — all without game knowledge. The FDN drives all variation through the protocol.

**Rationale:** The subclass model (original design) required per-game PDN code and duplicated effort. The terminal model is more principled: the PDN is a peripheral, not a participant in game logic. Adding a new game experience now means zero new PDN code. The analogy: a web browser is complete without per-site code; sites (FDN) drive all variation through the protocol (HTTP/HTML).

**Implication:** The `onControllerMounted/Loop/Dismounted` protected hooks from the original design are removed. There is no subclass to call them.

**Implication:** The Controller app state machine collapses to two states: `ControllerRoutingState` → `ControllerState`. No game-specific states are registered.

### 6. ControllerRoutingState transitions to single ControllerState

**Decision:** `ControllerRoutingState` receives `LAUNCH_APP(appId)`, stores the app ID, and transitions to `ControllerState` — the same state regardless of app ID. `ControllerState` stores the app ID as context but uses it only for display purposes (e.g., title screen), not for routing.

**Planned deprecation:** Both routing states (PDN and FDN) are designed to be deprecated once the comms architecture delivers a context object at device handshake time. That context will carry the app ID, eliminating the need for an explicit `LAUNCH_APP` exchange. The routing state exists to bridge until that work lands.

### 7. Pairing stays in Quickdraw; ControllerState presupposes a live FDN session

**Decision:** `SymbolState` and `SymbolMatchedState` remain in `Quickdraw`. `ControllerState` only applies after pairing completes. The seam is `SymbolMatchedState` → app jump to `Controller` app.

**Open question:** Does `SymbolMatchedState` listen for `LAUNCH_APP` and fire the jump, or does the `Controller` app launch unconditionally after symbol match? See open questions below.

### 8. FdnGameState base class is symmetric to ControllerState

**Decision:** `FdnGameState` extends `ConnectState<FDN>` and provides the same infrastructure pattern as `ControllerState`, mirrored for the FDN's role as sender/driver:

- Sealed `onStateMounted/Loop/Dismounted` with protected `onGameMounted/Loop/Dismounted` hooks
- Input registry: `map<CommandId, std::function<void(CommandPayload, SerialIdentifier)>>` — handlers receive port context
- `sendCommand(id, payload, port)` and `broadcastCommand(id, payload)` helpers
- `setGamepadConfig(GamepadConfig)` — concrete states call this to define the active command set and button bindings; base class broadcasts `GAMEPAD_CONFIG` to all active ports on mount and stores it for re-broadcast when new PDNs connect
- Pure virtual `appId() const` — concrete states declare their identity; base class sends `LAUNCH_APP(appId())` followed by `GAMEPAD_CONFIG` to all connected PDNs on mount

**Rationale:** The FDN side mirrors the PDN side in structure: sealed lifecycle, registry-based dispatch, base class owns the protocol ceremony (`LAUNCH_APP` + `GAMEPAD_CONFIG` on mount, giving connected PDNs their app context and input configuration in one sequence).

### 9. State-machine level command vocabulary

**Decision:** Each FDN game state machine defines its full command vocabulary. Each `FdnGameState` subclass calls `setGamepadConfig(GamepadConfig)` in its constructor or `onGameMounted`, providing a `GamepadConfig` whose `CommandSet` contains the subset of commands active for that state. The base class broadcasts this as `GAMEPAD_CONFIG` to all connected PDNs on mount.

**Open question:** Whether a concrete state can call `setGamepadConfig` mid-state (to dynamically update the command set or button bindings) requires further team discussion. The base class API supports it; whether it's encouraged is unresolved.

### 10. Multi-PDN: 1 or 2 PDNs, event-driven connect/disconnect

**Decision:** FDN experiences support exactly 1 or 2 connected PDNs. Connection and disconnection are event-driven via jack-changed callbacks (not polled). The base class maintains `activePorts: map<SerialIdentifier, MacAddress>` and a `knownDisconnectedPorts: map<SerialIdentifier, MacAddress>` with a TTL of ~10 seconds.

On disconnect: `onPdnDisconnected(port)` hook fires. Concrete game states define policy (end session, enter waiting state, etc.).
On reconnect: incoming connect event with a MAC matching `knownDisconnectedPorts` fires `onPdnReconnected(port)`. The `knownDisconnectedPorts` TTL prevents stale MAC accumulation.

**Reconnect UX:** The reconnect countdown (~10 seconds) SHALL be rendered on the PDN screen during the waiting period. The base class sends a `GAME_EVENT` with countdown text on each tick; concrete states do not implement this.

**Alternative considered:** Poll RDC port state in the loop (symmetric to PDN's `isPersistentlyDisconnected`). Rejected in favor of jack-changed callbacks — more efficient and aligns with the incoming comms architecture.

### 11. GamepadConfig — composable, FDN-driven input configuration

**Decision:** `GamepadConfig` is a composable configuration object sent by the FDN via a new `GAMEPAD_CONFIG` command on `kFdnCommand`. It contains:
- `CommandSet commandSet` — always present; defines all available commands with `id`, `label`, and `icon (ResourceId)`.
- `InputConfig input` — defines button-to-command mappings.
- `bool showInstructions` — if true, `ControllerState` renders an instruction screen on receipt.

`InputConfig` has a flat, per-button struct layout:
```
InputConfig {
    cycleConfirmConfig: bool,
    primaryButtonConfig: ButtonConfig,   // top button = PRIMARY_BUTTON
    secondaryButtonConfig: ButtonConfig  // bottom button = SECONDARY_BUTTON
}

ButtonConfig {
    buttonPress: CommandId,       // CommandId::NONE = unbound
    buttonDoubleClick: CommandId,
    buttonLongPress: CommandId,
    buttonRelease: CommandId      // maps to attachLongPressStop; only fires at end of long press
}
```

When `cycleConfirmConfig=true`, primary PRESS → internal `cycleSelection()` and secondary PRESS → internal `confirmSelection()` (sends `SELECTION_CONFIRMED` with current selection). The `buttonPress` fields on both `ButtonConfig` structs are ignored in this mode. All other interactions remain freely bindable regardless of the flag.

`CommandId::NONE` is a reserved sentinel value (e.g. 0) meaning "not bound." Unbound interactions are silently ignored — no callback registered.

`LONG_PRESS_DURATION_MS` is a `constexpr uint32_t` in `lib/core` — universal across both firmware targets.

**Composability:** `GamepadConfig` is designed for future module additions. `LedConfig` and `HapticConfig` are anticipated future fields. Each module is independently nullable; adding one does not break existing configs.

**Instruction screen:** When `showInstructions=true`, `ControllerState` generates instruction content from the config without any additional payload. For each bound non-cycle interaction: interaction type name + command label + command icon. For cycle/confirm interactions: fixed standard descriptions.

**Config updates:** The FDN can send a new `GAMEPAD_CONFIG` at any time. `ControllerState` atomically deregisters all current button callbacks and rewires per the new config. Standalone `COMMAND_SET` commands remain available for lightweight command list updates without changing button bindings.

**Rationale:** The original hardcoded top=cycle/bottom=confirm assumption works for simple experiences but fails for games requiring more expressive input (long press, double click, simultaneous binding of cycle + custom actions). The flat `ButtonConfig` struct avoids variant types, maps cleanly to the raw function pointer callback API (`parameterizedCallbackFunction`), serializes predictably to a fixed-size packet (max 8 bindable slots: 4 interactions × 2 buttons), and supports the hybrid case (cycleConfirm + custom long press) without any additional machinery.

## Risks / Trade-offs

- **Display formats beyond `{ text, icon }`** → If a future experience requires multi-zone display, animations, or richer layouts, `GameEventPayload` would need extending. Mitigation: design the payload to be additive; `ControllerState` drops unrecognized fields gracefully.

- **Button interactions beyond the v1 set** → v1 supports PRESS, DOUBLE_CLICK, LONG_PRESS, RELEASE. Any future interaction type (hold-and-release sequences, multi-button chords, etc.) requires adding a field to `ButtonConfig` and updating `ControllerState` wiring. Mitigation: `GamepadConfig` is versioned and extensible; unrecognized fields are ignored.

- **FDN must adopt new channels** → FDN firmware needs to switch from old packet types. Mitigation: keep old channel constants as deprecated aliases during transition.

- **`std::map` on embedded target** → Both command registries use `map`. Mitigation: profile after implementation; flat array with linear search is a drop-in replacement if needed.

- **Reconnect TTL needs tuning** → 10 seconds is an initial estimate. If network conditions cause spurious disconnects and reconnects faster than the TTL, the countdown UX may feel jarring. Mitigation: make TTL configurable per state machine.

- **Mid-state `GAMEPAD_CONFIG` updates** → Whether concrete FDN states can call `setGamepadConfig` mid-state to dynamically update commands or bindings requires further team discussion. Blocking on team decision.

## Migration Plan

1. Add `lib/core/include/controller/controller-command-types.hpp` — `kFdnCommand`, `kPdnInput`, command IDs, `AppId`, `ResourceId`, `Command`, `CommandSet`, `GameEventPayload`, `ButtonConfig`, `InputConfig`, `GamepadConfig`, `LONG_PRESS_DURATION_MS`.
2. Implement `ControllerState` (terminal concrete state) on PDN — `GAMEPAD_CONFIG` handler, `GAME_EVENT` handler, `LAUNCH_APP` handler, `COMMAND_SET` handler, disconnect poll.
3. Simplify `ControllerRoutingState` — transition to single `ControllerState` on `LAUNCH_APP`.
4. Update Controller app state machine — remove all game-specific state registrations.
5. Delete `Controller1State` and all existing game-specific `ControllerState` subclasses.
6. Implement `FdnGameState` base class on FDN — `setGamepadConfig`, `sendCommand`/`broadcastCommand`, input registry, jack-changed callbacks.
7. Implement `FdnGameRoutingState` on FDN.
8. Refactor existing FDN game states to extend `FdnGameState`.
9. Update `Quickdraw` pairing seam.
10. Update FDN firmware to use `kFdnCommand` / `kPdnInput`.
11. Remove deprecated old packet type aliases.

## Open Questions

1. **`SymbolMatchedState` boundary** — Does `SymbolMatchedState` listen for `LAUNCH_APP` and fire the app jump (couples `Quickdraw` to controller protocol), or does the `Controller` app launch unconditionally after symbol match?

2. **Mid-state `GAMEPAD_CONFIG` updates** — Can a concrete `FdnGameState` call `setGamepadConfig` mid-state to dynamically update available commands or button bindings? Needs team discussion.

3. **Reconnect TTL** — Is ~10 seconds the right default? Should it be configurable per state machine?
