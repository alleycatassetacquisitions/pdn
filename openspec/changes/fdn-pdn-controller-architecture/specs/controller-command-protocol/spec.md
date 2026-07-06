## MODIFIED Requirements

### Requirement: Shared command vocabulary in lib/core
Both PDN and FDN firmware SHALL build against a single shared header `lib/core/include/controller/controller-command-types.hpp` that defines all controller command IDs, channel identifiers, packet structs, `AppId`, and `ResourceId`. No firmware target SHALL define its own controller command types independently.

#### Scenario: Both targets include the shared header
- **WHEN** PDN firmware and FDN firmware are compiled
- **THEN** both include `controller-command-types.hpp` from `lib/core` and resolve all command IDs, `AppId`, `ResourceId`, and packet structs from the same definitions

### Requirement: FDN-to-PDN command channel
A dedicated `kFdnCommand` channel SHALL carry all commands from the FDN to the PDN. The command set SHALL include `LAUNCH_APP`, `COMMAND_SET`, `GAME_EVENT`, and `GAMEPAD_CONFIG`. The channel SHALL be designed to accommodate additional command IDs as new experiences are added.

#### Scenario: FDN sends LAUNCH_APP
- **WHEN** the FDN sends a `LAUNCH_APP` command on `kFdnCommand`
- **THEN** the PDN receives a packet containing an `AppId` identifying which application to run

#### Scenario: FDN sends COMMAND_SET
- **WHEN** the FDN sends a `COMMAND_SET` command on `kFdnCommand` as a lightweight standalone update
- **THEN** the PDN receives an ordered list of `Command` objects, each with a `CommandId`, a display label string, and a `ResourceId` icon; existing button bindings are unchanged

#### Scenario: FDN sends GAME_EVENT
- **WHEN** the FDN sends a `GAME_EVENT` command on `kFdnCommand`
- **THEN** the PDN receives a `GameEventPayload` with nullable `text` and nullable `icon` fields

### Requirement: GameEventPayload carries pre-formatted display data
`GameEventPayload` SHALL be defined in `lib/core` with two nullable fields: `text` (string, nullable) and `icon` (`ResourceId`, nullable). The FDN SHALL pre-format all display content. The PDN SHALL render the text and icon without any game-specific interpretation. Both fields being null is valid (no-op render update).

#### Scenario: FDN sends a text-only game event
- **WHEN** the FDN sends `GAME_EVENT` with a non-null `text` and null `icon`
- **THEN** the PDN renders the text string; the PDN owns wrapping and layout

#### Scenario: FDN sends an icon-only game event
- **WHEN** the FDN sends `GAME_EVENT` with a null `text` and non-null `icon`
- **THEN** the PDN looks up the `ResourceId` in its local resource table and renders the corresponding asset

#### Scenario: FDN sends a full game event
- **WHEN** the FDN sends `GAME_EVENT` with both `text` and `icon` non-null
- **THEN** the PDN renders both the text and the icon asset

### Requirement: FDN sends GAMEPAD_CONFIG to configure PDN input
The FDN SHALL send `GAMEPAD_CONFIG` on `kFdnCommand` to configure the PDN's button bindings and active command set. The payload SHALL be a `GamepadConfig` struct. The FDN MAY send a new `GAMEPAD_CONFIG` at any time to update the configuration.

#### Scenario: FDN sends initial GAMEPAD_CONFIG on session start
- **WHEN** the FDN mounts a game state and the PDN has entered `ControllerState`
- **THEN** the FDN sends `GAMEPAD_CONFIG` carrying the game's command set and input bindings

#### Scenario: FDN updates config mid-session
- **WHEN** game logic requires different button bindings (e.g. phase transition)
- **THEN** the FDN sends a new `GAMEPAD_CONFIG`; the PDN atomically replaces its button configuration

### Requirement: PDN-to-FDN input channel
A dedicated `kPdnInput` channel SHALL carry all input events from the PDN to the FDN. The initial event set SHALL include `SELECTION_CONFIRMED`. The PDN SHALL NOT send raw button identifiers on this channel.

#### Scenario: PDN confirms selection
- **WHEN** the player confirms a selection on the PDN
- **THEN** the PDN sends `SELECTION_CONFIRMED` carrying the `CommandId` of the currently selected command option — not a physical button identifier

#### Scenario: No raw button IDs on kPdnInput
- **WHEN** the PDN sends any packet on `kPdnInput`
- **THEN** the packet carries a semantic `CommandId`, never a raw hardware button identifier

### Requirement: Echo-confirmation pattern is removed
The FDN SHALL NOT echo received PDN input back to the PDN as an acknowledgment. The FDN SHALL push updated game state via `GAME_EVENT` or `COMMAND_SET` only when game logic warrants it.

#### Scenario: FDN receives SELECTION_CONFIRMED
- **WHEN** the FDN receives `SELECTION_CONFIRMED` from the PDN
- **THEN** the FDN updates game state internally and pushes `GAME_EVENT` or `COMMAND_SET` only if game logic requires it — not as a mandatory echo
