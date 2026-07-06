## ADDED Requirements

### Requirement: FdnGameState is a ConnectState<FDN> subclass
`FdnGameState` SHALL extend `ConnectState<FDN>` and provide the infrastructure for all FDN game states. It SHALL NOT be a terminal state — concrete FDN game states extend it.

#### Scenario: Concrete FDN game state extends FdnGameState
- **WHEN** a developer implements a new FDN game state
- **THEN** it extends `FdnGameState` and overrides `onGameMounted`, `onGameLoop`, and/or `onGameDismounted` — never the sealed lifecycle methods

### Requirement: FdnGameState declares app identity via pure virtual method
`FdnGameState` SHALL declare a pure virtual `appId() const` method. Concrete states SHALL implement it to return their `AppId`. The base class SHALL use this to send `LAUNCH_APP(appId())` to all connected PDNs on mount.

#### Scenario: FdnGameState mounts
- **WHEN** a concrete `FdnGameState` mounts
- **THEN** the base class broadcasts `LAUNCH_APP(appId())` to all connected PDN ports before calling `onGameMounted`

### Requirement: Lifecycle hooks are sealed on the base class
`FdnGameState` SHALL seal `onStateMounted`, `onStateLoop`, and `onStateDismounted` as `final`. The base class SHALL expose protected hooks `onGameMounted`, `onGameLoop`, and `onGameDismounted` that concrete states override.

#### Scenario: Concrete game state uses protected hooks
- **WHEN** a concrete `FdnGameState` subclass is implemented
- **THEN** it overrides `onGameMounted`, `onGameLoop`, and/or `onGameDismounted` — never the sealed methods

### Requirement: Input registry dispatches kPdnInput to concrete state handlers
The base class SHALL maintain a `map<CommandId, std::function<void(CommandPayload, SerialIdentifier)>>`. Incoming `kPdnInput` packets SHALL be dispatched through this registry. Each handler receives both the payload and the port it arrived from. Concrete states SHALL register handlers via `registerInputHandler(id, callback)`.

#### Scenario: PDN sends SELECTION_CONFIRMED
- **WHEN** a connected PDN sends `SELECTION_CONFIRMED` on `kPdnInput`
- **THEN** the registered handler is invoked with the payload and the `SerialIdentifier` of the originating port

#### Scenario: Unregistered command dropped with warning
- **WHEN** a PDN sends a `kPdnInput` command not registered by the concrete state
- **THEN** the command is dropped and a warning is logged; no crash occurs

### Requirement: FdnGameState provides sendCommand and broadcastCommand helpers
The base class SHALL provide `sendCommand(CommandId, CommandPayload, SerialIdentifier port)` to send a command to a specific connected PDN, and `broadcastCommand(CommandId, CommandPayload)` to send to all active ports. Concrete states SHALL use these helpers rather than calling the wireless layer directly.

#### Scenario: Concrete state sends a command to one PDN
- **WHEN** a concrete state calls `sendCommand(GAME_EVENT, payload, port)`
- **THEN** the command is sent to the PDN connected on that port

#### Scenario: Concrete state broadcasts to all PDNs
- **WHEN** a concrete state calls `broadcastCommand(GAME_EVENT, payload)`
- **THEN** the command is sent to all ports in `activePorts`

### Requirement: GamepadConfig is managed by FdnGameState and broadcast on mount
The base class SHALL expose `setGamepadConfig(GamepadConfig)`. When called, it SHALL store the config and broadcast `GAMEPAD_CONFIG` to all connected PDN ports. The config SHALL also be stored for re-broadcast to any PDN that connects after initial mount. On mount, the base class SHALL broadcast `GAMEPAD_CONFIG` after `LAUNCH_APP` if the concrete state has called `setGamepadConfig` before or during `onGameMounted`.

#### Scenario: Concrete state defines its GamepadConfig
- **WHEN** a concrete state calls `setGamepadConfig(...)` in its constructor or `onGameMounted`
- **THEN** the base class broadcasts `GAMEPAD_CONFIG` to all active PDN ports

#### Scenario: GamepadConfig broadcast follows LAUNCH_APP on mount
- **WHEN** `FdnGameState` mounts
- **THEN** `LAUNCH_APP` is broadcast first, then `GAMEPAD_CONFIG` (if set), then `onGameMounted` is called
