## Why

The `fdn-experience` branch proves that a PDN can pair with an FDN and act as a wireless game controller, but the communication layer uses ad-hoc packet types and per-state boilerplate. The original architecture proposed a base class / subclass model where each new FDN game experience would require a new concrete PDN state subclass. Further design exploration revealed a superior model: the PDN Controller app can be a **protocol terminal** — a complete, fixed implementation that handles all FDN experiences generically through the command protocol, requiring zero new PDN code per game. All game variation lives in the FDN. This change establishes both the unified protocol and this terminal model on the PDN side, and defines the symmetric FDN game state architecture.

## What Changes

- `GAME_SELECT` renamed to `LAUNCH_APP` with `AppId` — broadens the concept to support any application (including the game selection menu itself), not just game IDs.
- `GameEventPayload` struct added to `lib/core` — `{ text?: string, icon?: ResourceId }`, both fields nullable. The FDN pre-formats display content; the PDN renders it without game-specific knowledge.
- `ControllerState` is a **terminal concrete state**, not a base class — handles all FDN experiences via `COMMAND_SET` + `GAME_EVENT` protocol. No game-specific PDN subclasses exist or are needed. Adding a new FDN game requires **zero PDN code changes**.
- `ControllerRoutingState` simplified — transitions to the single `ControllerState` on `LAUNCH_APP`; planned for deprecation when the comms architecture delivers a context object at handshake time.
- Controller app state machine collapses to two states: `ControllerRoutingState` → `ControllerState`.
- Replace `kGameSelect`, `kControllerCommand`, `kGameResponse` with two clean channels: `kFdnCommand` (FDN→PDN) and `kPdnInput` (PDN→FDN), defined in `lib/core`.
- Remove echo-confirmation pattern (FDN stops echoing button IDs back to PDN).
- `FdnGameState` base class introduced on the FDN — symmetric to `ControllerState`: sealed lifecycle hooks, input registry dispatching `kPdnInput`, `sendCommand`/`broadcastCommand` helpers, command set management.
- `FdnGameRoutingState` introduced — symmetric to `ControllerRoutingState`: supports direct launch (FDN hardcodes app) or interactive selection (FDN presents game menu via `COMMAND_SET`, waits for `SELECTION_CONFIRMED`). Planned for same deprecation path.
- Multi-PDN support defined — FDN experiences support 1 or 2 connected PDNs. Disconnect and reconnect are event-driven via jack-changed callbacks. Reconnect TTL of ~10 seconds rendered as a countdown on-screen. `onPdnDisconnected`/`onPdnReconnected` hooks on `FdnGameState` allow concrete game states to define their own disconnect policy.
- State-machine level command vocabulary — each FDN game state machine defines its full command vocabulary; each `FdnGameState` subclass compiles a subset into its active command set.
- `GamepadConfig` introduced — a composable FDN-sent configuration object that defines both the PDN's active command set and button bindings. Contains `CommandSet` (commands with id, label, icon), `InputConfig` (button-to-command mapping with `cycleConfirmConfig` flag), and `showInstructions` flag. Replaces the hardcoded top=cycle/bottom=confirm assumption. Future-extensible with `LedConfig`, `HapticConfig`, etc.
- `InputConfig` uses a flat per-button struct (`primaryButtonConfig` / `secondaryButtonConfig`) with nullable `CommandId` fields per interaction (PRESS, DOUBLE_CLICK, LONG_PRESS, RELEASE). When `cycleConfirmConfig=true`, primary PRESS → internal cycle, secondary PRESS → internal confirm, overriding those fields; all other interactions remain freely bindable. `CommandId::NONE` is the null sentinel for unbound slots.
- `LONG_PRESS_DURATION_MS` defined as a `constexpr` in `lib/core` — universal across all devices.

## Capabilities

### New Capabilities

- `fdn-game-state-base`: `FdnGameState` PDN-symmetric base class for FDN game states — sealed lifecycle, input registry, `sendCommand`/`broadcastCommand`, command set management, automatic `LAUNCH_APP` broadcast on mount.
- `fdn-game-routing-state`: `FdnGameRoutingState` provided concrete state — direct launch and interactive selection modes; planned deprecation path documented.
- `fdn-multi-pdn-support`: Multi-PDN session management — 1 or 2 PDN support, jack-changed callback integration, reconnect TTL with countdown, `onPdnDisconnected`/`onPdnReconnected` hooks.
- `gamepad-config`: Composable `GamepadConfig` object — `CommandSet`, `InputConfig` with `cycleConfirmConfig` flag and per-button nullable interaction bindings, `showInstructions` flag. Sent via `GAMEPAD_CONFIG` command on `kFdnCommand`. Instruction screen auto-generated from command label, icon, and interaction type name.

### Modified Capabilities

- `controller-command-protocol`: `GAME_SELECT` → `LAUNCH_APP` + `AppId`; `GameEventPayload` struct added (nullable text, nullable icon ResourceId); `GAMEPAD_CONFIG` command added on `kFdnCommand`; `CommandId::NONE` sentinel added; `Command` struct in `CommandSet` gains `icon: ResourceId` field.
- `controller-state-base`: `ControllerState` reframed as terminal concrete state; `GAME_EVENT` handler added (render text + icon); `LAUNCH_APP` handler added (update app context); `GAMEPAD_CONFIG` handler added — deregisters all button callbacks and rewires per new config; instruction screen rendered when `showInstructions=true`.
- `controller-routing-state`: Simplified — transitions to single `ControllerState` on `LAUNCH_APP`; planned deprecation noted.
- `controller-app-assembly`: State machine collapses to two states (`ControllerRoutingState` → `ControllerState`); no game-specific states; adding a new FDN game requires no new PDN states.

## Impact

- **PDN firmware**: `Controller` app simplified to two states; `Controller1State` and all existing game-specific subclasses removed; `ControllerState` becomes a complete terminal implementation.
- **FDN firmware**: New `FdnGameState` base class and `FdnGameRoutingState`; existing game states refactored to extend `FdnGameState`; protocol updated to `kFdnCommand`/`kPdnInput`.
- **`lib/core`**: `controller-command-types.hpp` adds `LAUNCH_APP`, `GameEventPayload`, `ResourceId`, `GamepadConfig`, `InputConfig`, `ButtonConfig`, `CommandId::NONE`, `LONG_PRESS_DURATION_MS`; both firmware targets gain these definitions.
- **`Quickdraw` state machine**: `SymbolMatchedState` triggers app jump to `Controller`; exact boundary (listens for `LAUNCH_APP` vs. launches unconditionally) remains an open question.
- **`ControllerWirelessManager`**: Per-port MAC routing deferred to comms rewrite; jack-changed callback integration required for multi-PDN disconnect/reconnect.
