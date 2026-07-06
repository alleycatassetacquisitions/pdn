## 1. Shared Command Protocol (lib/core)

- [ ] 1.1 Create `lib/core/include/controller/controller-command-types.hpp` with `kFdnCommand` and `kPdnInput` channel identifiers
- [ ] 1.2 Define `CommandId` enum with `LAUNCH_APP`, `COMMAND_SET`, `GAME_EVENT`, `GAMEPAD_CONFIG`, `SELECTION_CONFIRMED`, and `NONE` sentinel
- [ ] 1.3 Define `AppId` enum with entries for all supported apps (games + selection menu)
- [ ] 1.4 Define `ResourceId` enum for PDN display assets
- [ ] 1.5 Define `Command` struct: `CommandId id`, `string label`, `ResourceId icon`
- [ ] 1.6 Define `CommandSet` struct: ordered list of `Command` objects
- [ ] 1.7 Define `GameEventPayload` struct with nullable `text` (string) and nullable `icon` (ResourceId)
- [ ] 1.8 Add `LONG_PRESS_DURATION_MS` as `constexpr uint32_t`
- [ ] 1.9 Verify both PDN and FDN firmware targets compile successfully with the new header

## 2. GamepadConfig Types (lib/core)

- [ ] 2.1 Define `ButtonConfig` struct: four `CommandId` fields (`buttonPress`, `buttonDoubleClick`, `buttonLongPress`, `buttonRelease`)
- [ ] 2.2 Define `InputConfig` struct: `bool cycleConfirmConfig`, `ButtonConfig primaryButtonConfig`, `ButtonConfig secondaryButtonConfig`
- [ ] 2.3 Define `GamepadConfig` struct: `CommandSet commandSet`, `InputConfig input`, `bool showInstructions`

## 3. ControllerState — Terminal Concrete State (PDN)

- [ ] 3.1 Create `ControllerState` as a terminal concrete class extending `ConnectState<PDN>` — not designed for subclassing
- [ ] 3.2 Seal `onStateMounted`, `onStateLoop`, `onStateDismounted` as `final`; no protected lifecycle hooks
- [ ] 3.3 Add fixed-size `BindingContext bindingContexts[8]` member array to hold per-binding `void*` context for raw function pointer callbacks
- [ ] 3.4 Implement `applyInputConfig(const InputConfig&)`: deregister all button callbacks, wire per `cycleConfirmConfig` flag and per non-NONE field
- [ ] 3.5 Implement `cycleSelection()`: advance `selectionIndex`, wrap around, render current command label
- [ ] 3.6 Implement `confirmSelection()`: send `SELECTION_CONFIRMED(activeCommandSet[selectionIndex].id)` on `kPdnInput`
- [ ] 3.7 Implement `GAMEPAD_CONFIG` handler: replace `CommandSet`, call `applyInputConfig`, render instruction screen if `showInstructions=true`
- [ ] 3.8 Implement instruction screen generation: per active binding, show interaction name + command label + command icon
- [ ] 3.9 Implement `COMMAND_SET` handler: replace active command set without rewiring buttons
- [ ] 3.10 Implement `GAME_EVENT` handler: render `payload.text` (if non-null) and look up + render `payload.icon` (if non-null)
- [ ] 3.11 Implement `LAUNCH_APP` handler: store `AppId` as current app context; optionally update title display
- [ ] 3.12 Implement disconnect poll: set `transitionToQuickdrawState = true` on `isPersistentlyDisconnected()`
- [ ] 3.13 Implement button deregistration on dismount
- [ ] 3.14 Implement `kFdnCommand` dispatch: route to internal handlers, drop + warn on unregistered IDs

## 4. ControllerRoutingState — Simplified (PDN)

- [ ] 4.1 Update `ControllerRoutingState` to extend `ConnectState<PDN>` directly (not `ControllerState`)
- [ ] 4.2 On mount: render neutral "connecting…" screen
- [ ] 4.3 Register `LAUNCH_APP` handler: store `AppId`, set transition flag to `ControllerState`
- [ ] 4.4 Implement disconnect → Quickdraw jump
- [ ] 4.5 Add planned-deprecation comment referencing comms architecture context object

## 5. Controller App State Machine — Simplified (PDN)

- [ ] 5.1 Resolve `SymbolMatchedState` boundary question (team decision: listens for `LAUNCH_APP` vs. launches unconditionally)
- [ ] 5.2 Update `SymbolMatchedState` in `Quickdraw` per the agreed boundary
- [ ] 5.3 Assemble Controller app with exactly two states: `ControllerRoutingState` (entry) → `ControllerState`
- [ ] 5.4 Wire single transition: `ControllerRoutingState` → `ControllerState`, passing `AppId` as context
- [ ] 5.5 Delete `Controller1State` and all existing game-specific `ControllerState` subclasses
- [ ] 5.6 Verify no game-specific state registrations remain in the Controller app

## 6. FdnGameState Base Class (FDN)

- [ ] 6.1 Create `FdnGameState` abstract class extending `ConnectState<FDN>`
- [ ] 6.2 Declare pure virtual `appId() const`
- [ ] 6.3 Seal `onStateMounted`, `onStateLoop`, `onStateDismounted` as `final`; expose protected `onGameMounted`, `onGameLoop`, `onGameDismounted`
- [ ] 6.4 Implement `registerInputHandler(id, callback(payload, port))` and `kPdnInput` dispatch
- [ ] 6.5 Implement `sendCommand(id, payload, port)` and `broadcastCommand(id, payload)`
- [ ] 6.6 Implement `setGamepadConfig(GamepadConfig)`: broadcast `GAMEPAD_CONFIG` to all active ports; store for re-broadcast on new PDN connect
- [ ] 6.7 Implement mount sequence: broadcast `LAUNCH_APP(appId())` → broadcast `GAMEPAD_CONFIG` (if set) → call `onGameMounted`

## 7. FdnGameRoutingState (FDN)

- [ ] 7.1 Create `FdnGameRoutingState` extending `FdnGameState`
- [ ] 7.2 Implement direct launch mode: on mount, broadcast `LAUNCH_APP(targetAppId)` and set transition flag
- [ ] 7.3 Implement interactive selection mode: on mount, send `GAMEPAD_CONFIG` with available apps as command set; register `SELECTION_CONFIRMED` handler
- [ ] 7.4 On `SELECTION_CONFIRMED(appId)` in interactive mode: broadcast `LAUNCH_APP(appId)`, transition to registered game state
- [ ] 7.5 Log warning and remain active on unknown `AppId`
- [ ] 7.6 Add planned-deprecation comment referencing comms architecture context object

## 8. Multi-PDN Support (FDN)

- [ ] 8.1 Add `activePorts: map<SerialIdentifier, MacAddress>` and `knownDisconnectedPorts: map<SerialIdentifier, pair<MacAddress, timestamp>>` to `FdnGameState`
- [ ] 8.2 Wire jack-changed callbacks: connect event → MAC match check → `onPdnConnected` or `onPdnReconnected`; disconnect event → `onPdnDisconnected`
- [ ] 8.3 Implement TTL eviction for `knownDisconnectedPorts` (~10 seconds; confirm final value with team)
- [ ] 8.4 Expose protected `onPdnConnected(port)`, `onPdnDisconnected(port)`, `onPdnReconnected(port)` hooks
- [ ] 8.5 Implement reconnect countdown: on disconnect, start TTL ticker; send `GAME_EVENT` with countdown text to remaining active ports each second
- [ ] 8.6 Stop countdown on `onPdnReconnected`; resume normal game state

## 9. FDN Protocol Update

- [ ] 9.1 Refactor existing FDN game states to extend `FdnGameState`; remove manual wireless callback registration and disconnect logic
- [ ] 9.2 Update FDN firmware to send `LAUNCH_APP`, `GAMEPAD_CONFIG`, `GAME_EVENT` on `kFdnCommand` (replacing old packet types)
- [ ] 9.3 Update FDN firmware to receive `SELECTION_CONFIRMED` on `kPdnInput` (replacing `kControllerCommand`)
- [ ] 9.4 Remove echo-confirmation logic from FDN game states
- [ ] 9.5 Remove deprecated `kGameSelect`, `kControllerCommand`, `kGameResponse` aliases once both sides are updated

## 10. Validation

- [ ] 10.1 End-to-end: PDN pairs → `ControllerRoutingState` shows neutral screen → FDN sends `LAUNCH_APP` + `GAMEPAD_CONFIG` → `ControllerState` active with correct bindings
- [ ] 10.2 Test: `GAMEPAD_CONFIG` with `cycleConfirmConfig=true` → top button cycles → bottom button confirms → FDN receives `SELECTION_CONFIRMED` with correct `CommandId`
- [ ] 10.3 Test: `GAMEPAD_CONFIG` with `cycleConfirmConfig=false` and direct bindings → each interaction fires correct `SELECTION_CONFIRMED(commandId)`
- [ ] 10.4 Test: `cycleConfirmConfig=true` + `secondaryButtonConfig.buttonLongPress` bound → confirm and long press both work independently
- [ ] 10.5 Test: `showInstructions=true` → instruction screen renders with interaction name + command label + command icon for each active binding
- [ ] 10.6 Test: new `GAMEPAD_CONFIG` mid-session → old callbacks fully deregistered before new ones registered; no overlap
- [ ] 10.7 Test: `CommandId::NONE` binding → no callback registered, no crash on interaction
- [ ] 10.8 Test: standalone `COMMAND_SET` mid-session → command list updated, button bindings unchanged
- [ ] 10.9 Test: `GAME_EVENT(text, icon)` → PDN renders both; `GAME_EVENT(text, null)` → text only; `GAME_EVENT(null, icon)` → icon only
- [ ] 10.10 Test: FDN disconnects mid-session → PDN returns to `Quickdraw` without leaked button callbacks
- [ ] 10.11 Test (2-PDN): one PDN disconnects → `onPdnDisconnected` fires → countdown renders on remaining PDN
- [ ] 10.12 Test (2-PDN): disconnected PDN reconnects within TTL → `onPdnReconnected` fires → countdown stops
- [ ] 10.13 Test: FDN routing interactive mode → PDN selects app → FDN sends `LAUNCH_APP` → both sides transition correctly
