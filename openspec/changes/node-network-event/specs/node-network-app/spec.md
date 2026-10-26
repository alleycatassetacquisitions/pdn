## ADDED Requirements

### Requirement: NodeNetwork is a standalone app with embedded PlayerFetch
The firmware SHALL provide a `NodeNetwork` state machine with its own app id. It SHALL construct and own a `PlayerProvider` and a `PlayerFetchApp` wired with that provider, mirroring how Quickdraw embeds `PlayerRegistrationApp`.

#### Scenario: Provider ownership
- **WHEN** NodeNetwork is constructed
- **THEN** fetch states and MatchManagerV2 receive the same `PlayerProvider` instance

### Requirement: Wireless callbacks target MatchManagerV2 only
NodeNetwork SHALL register the NodeNetwork duel packet handler to `MatchManagerV2` through `NodeNetworkWirelessManager`. It SHALL NOT register `MatchManager`, `QuickdrawWirelessManager`, chain, shootout, or symbol packet handlers on the event app class.

#### Scenario: Event app handlers
- **WHEN** NodeNetwork is the active app
- **THEN** duel ESP-NOW events are delivered only to MatchManagerV2

### Requirement: Gameplay handoff from PlayerFetch
NodeNetwork SHALL transition from the embedded PlayerFetch subgraph into Node gameplay states when `PlayerFetchApp::readyForGameplay()` becomes true (awaken complete on the server path). Demo stub path SHALL NOT enter Node gameplay until product defines otherwise.

#### Scenario: Server registration path
- **WHEN** awaken completes after successful fetch
- **THEN** NodeNetwork enters Node idle (or equivalent first gameplay state)

#### Scenario: Demo path
- **WHEN** demo stub is active
- **THEN** Node idle is not entered via readyForGameplay

### Requirement: Default Quickdraw boot unchanged
The default PlatformIO environment that boots Quickdraw SHALL NOT mount NodeNetwork or change Quickdraw’s use of `PlayerRegistrationApp`. NodeNetwork is for the event firmware entry only until an explicit later cutover.

#### Scenario: Normal firmware
- **WHEN** device boots default Quickdraw config
- **THEN** PlayerFetch is not part of the active state map

### Requirement: Event firmware mounts NodeNetwork only
The event PlatformIO environment SHALL load `NodeNetwork` as the default app in `AppConfig`. Quickdraw MAY remain compilable but SHALL NOT be inserted into the loaded app map for that environment.

#### Scenario: Event boot
- **WHEN** event env setup runs
- **THEN** the device state machine root is NodeNetwork
- **AND** Quickdraw is not a loaded app
