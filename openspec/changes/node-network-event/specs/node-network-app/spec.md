## ADDED Requirements

### Requirement: NodeNetwork is a standalone app with embedded PlayerFetch
The firmware SHALL provide a `NodeNetwork` state machine with its own app id. It SHALL construct and own a `PlayerProvider` and a `PlayerFetchApp` wired with that provider, mirroring how Quickdraw embeds `PlayerRegistrationApp`.

#### Scenario: Provider ownership
- **WHEN** NodeNetwork is constructed
- **THEN** fetch states and MatchManagerV2 receive the same `PlayerProvider` instance

### Requirement: Wireless callbacks target MatchManagerV2 for duels
NodeNetwork SHALL register the NodeNetwork duel packet handler to `MatchManagerV2` through `NodeNetworkWirelessManager`. It SHALL NOT register `MatchManager`, `QuickdrawWirelessManager`, chain, or shootout packet handlers.

#### Scenario: Duel handlers
- **WHEN** NodeNetwork is the active app
- **THEN** NodeNetwork duel ESP-NOW events are delivered only to MatchManagerV2

### Requirement: Symbol wireless on NodeNetwork
NodeNetwork SHALL register `SymbolWirelessManager` for FDN symbol match (`PktType::kSymbolMatchCommand`) when symbol states are included in the app.

#### Scenario: Symbol handler
- **WHEN** NodeNetwork is the active app and symbol match is in scope
- **THEN** symbol ESP-NOW commands are handled by `SymbolWirelessManager`

### Requirement: Gameplay handoff from PlayerFetch
NodeNetwork SHALL transition from the embedded PlayerFetch subgraph into Node gameplay states when `PlayerFetchApp::readyForGameplay()` becomes true (awaken complete on the server path). Demo stub path SHALL NOT enter Node gameplay until product defines otherwise.

#### Scenario: Server registration path
- **WHEN** awaken completes after successful fetch
- **THEN** NodeNetwork enters Node idle (or equivalent first gameplay state)

#### Scenario: Demo path
- **WHEN** demo stub is active
- **THEN** Node idle is not entered via readyForGameplay

### Requirement: Default Quickdraw boot unchanged
When `main` mounts Quickdraw as the default app, it SHALL NOT mount NodeNetwork. (NodeNetwork may be the default app in event firmware builds.)

#### Scenario: Quickdraw firmware
- **WHEN** device boots a Quickdraw-first configuration
- **THEN** PlayerFetch is not part of the active state map unless that configuration explicitly mounts NodeNetwork
