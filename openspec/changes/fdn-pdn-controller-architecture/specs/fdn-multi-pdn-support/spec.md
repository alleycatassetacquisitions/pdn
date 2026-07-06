## ADDED Requirements

### Requirement: FDN experiences support 1 or 2 connected PDNs
`FdnGameState` SHALL support sessions with either 1 or 2 simultaneously connected PDNs. Sessions with more than 2 PDNs are not supported in this architecture revision. Each connected PDN is identified by its `SerialIdentifier`.

#### Scenario: Single-PDN session
- **WHEN** exactly one PDN is connected to the FDN during a game session
- **THEN** all commands are sent to that PDN's port; `broadcastCommand` sends to the single active port

#### Scenario: Two-PDN session
- **WHEN** two PDNs are connected to the FDN during a game session
- **THEN** `broadcastCommand` sends to both ports; `sendCommand` allows targeting either port individually

### Requirement: Connection and disconnection are event-driven via jack-changed callbacks
`FdnGameState` SHALL use jack-changed callbacks from the wireless layer to detect PDN connections and disconnections. It SHALL NOT poll port state in the loop.

#### Scenario: PDN connects
- **WHEN** a jack-changed callback fires with a new connection event
- **THEN** the port is added to `activePorts` and `onPdnConnected(port)` is called on the concrete game state

#### Scenario: PDN disconnects
- **WHEN** a jack-changed callback fires with a disconnect event
- **THEN** the port is removed from `activePorts`, its MAC is retained in `knownDisconnectedPorts` with the current timestamp, and `onPdnDisconnected(port)` is called on the concrete game state

### Requirement: Concrete states define their own disconnect policy via onPdnDisconnected hook
`FdnGameState` SHALL expose a protected `onPdnDisconnected(SerialIdentifier port)` hook. Concrete game states SHALL override it to implement game-rule-specific disconnect behavior (e.g., end session, enter waiting state, continue with remaining PDNs).

#### Scenario: One PDN disconnects in a two-PDN game
- **WHEN** one PDN disconnects during a two-PDN game session
- **THEN** `onPdnDisconnected(port)` fires on the concrete game state; the concrete state determines whether to end the session or enter a waiting/paused state

#### Scenario: The only PDN disconnects in a single-PDN game
- **WHEN** the sole connected PDN disconnects
- **THEN** `onPdnDisconnected(port)` fires; the concrete game state implements the appropriate end-of-session logic

### Requirement: Reconnect is detected by MAC matching with a TTL
When a connect event arrives, `FdnGameState` SHALL check if the incoming MAC matches any entry in `knownDisconnectedPorts`. If a match is found within the TTL (~10 seconds), it SHALL fire `onPdnReconnected(port)` instead of `onPdnConnected`. Entries in `knownDisconnectedPorts` that exceed the TTL SHALL be evicted.

#### Scenario: PDN reconnects within TTL
- **WHEN** a connect event arrives with a MAC matching a recently disconnected port within the ~10 second TTL
- **THEN** `onPdnReconnected(port)` fires on the concrete game state; the port is moved from `knownDisconnectedPorts` back to `activePorts`

#### Scenario: Reconnect TTL expires before PDN returns
- **WHEN** the TTL elapses and the disconnected PDN has not returned
- **THEN** the entry is evicted from `knownDisconnectedPorts`; if the PDN subsequently connects, it is treated as a new connection via `onPdnConnected`

#### Scenario: New unknown PDN connects
- **WHEN** a connect event arrives with a MAC not in `knownDisconnectedPorts`
- **THEN** `onPdnConnected(port)` fires; it is treated as a fresh connection

### Requirement: Reconnect countdown is rendered on the PDN
During the reconnect TTL window, `FdnGameState` SHALL send `GAME_EVENT` updates to remaining connected PDNs with a countdown display indicating the time remaining for the disconnected PDN to reconnect. The countdown SHALL render as `text` in the `GameEventPayload`.

#### Scenario: Countdown displayed on remaining PDN
- **WHEN** one PDN disconnects in a two-PDN session and the TTL countdown begins
- **THEN** the FDN sends `GAME_EVENT` ticks to the remaining connected PDN with countdown text (e.g., "Waiting for player 2... 9")

#### Scenario: Countdown resolves on reconnect
- **WHEN** the disconnected PDN reconnects before TTL expires
- **THEN** the FDN stops the countdown and resumes normal game state on both PDNs
