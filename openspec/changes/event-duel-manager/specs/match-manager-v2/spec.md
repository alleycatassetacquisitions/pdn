## ADDED Requirements

### Requirement: Coordinator composes modules
`MatchManagerV2` SHALL own or reference `DuelSession`, `DuelProtocol`, `MatchRecordQueue`, and use `duel-sync-evaluator` and `QuickdrawMatchRecordBuilder`. It SHALL present one facade to Node gameplay states. It SHALL NOT duplicate legacy `MatchManager`’s full surface area.

#### Scenario: No legacy Match accessor
- **WHEN** gameplay code uses MatchManagerV2
- **THEN** there is no `getCurrentMatch()` returning `Match`

### Requirement: PlayerProvider dependency
The coordinator SHALL use `PlayerProvider` for local `PlayerV2` identity. It SHALL NOT require legacy `Player` for duel operation.

#### Scenario: No profile
- **WHEN** finalize is attempted without `hasPlayer()`
- **THEN** finalize returns false

### Requirement: Local link side configuration
Before handshake or duel, gameplay SHALL set local link side (initiator on OUTPUT jack path, responder on INPUT). The coordinator SHALL reject initiator-only actions when configured as responder.

#### Scenario: Responder cannot start
- **WHEN** local side is responder and startMatchAsInitiator is called
- **THEN** no outbound SEND_MATCH_ID is sent

### Requirement: Inbound commands entry point
The coordinator SHALL expose a single entry point for wireless callbacks (e.g. `onQuickdrawCommand`) that delegates to `DuelProtocol`.

#### Scenario: Wireless callback
- **WHEN** QuickdrawWirelessManager delivers a command
- **THEN** the coordinator updates session via protocol only

### Requirement: Local draw entry point
The coordinator SHALL expose `onLocalDrawTimeMs` (or equivalent) for states to report reaction time after button handling. It SHALL update session and send DRAW_RESULT via protocol. **Preferred:** states wire buttons; coordinator does not register `getDuelButtonPush()` in `initialize()`.

#### Scenario: State-driven press
- **WHEN** duel state computes 215 ms reaction and calls onLocalDrawTimeMs(215)
- **THEN** session records local draw on the correct initiator/responder slot
- **AND** DRAW_RESULT is sent to opponent MAC

### Requirement: Sync and finalize gates
The coordinator SHALL expose `didSync()` using `evaluateDidSync` with session-derived inputs and `NODE_SYNC_WINDOW_MS`. `finalizeSyncedMatch()` (or `finalizeMatch`) SHALL append to `MatchRecordQueue` only when `didSync()` is true and builder succeeds.

#### Scenario: Fail sync
- **WHEN** didSync is false
- **THEN** finalize returns false

#### Scenario: Success
- **WHEN** didSync is true and opponent id is known
- **THEN** finalize returns true and queue count increases by one

### Requirement: Local failed link counter
The coordinator SHALL maintain `failed_attempts` per opponent MAC for UX. `recordLinkOutcome(false)` increments when sync fails; success or MAC change resets. Counter SHALL NOT be written into stored records.

#### Scenario: Two fails same peer
- **WHEN** recordLinkOutcome(false) twice without MAC change
- **THEN** getFailedAttempts returns 2

### Requirement: Handshake readiness
The coordinator SHALL expose `isHandshakeReady()` mirroring session/protocol handshake-ready state for idle → countdown transitions.

#### Scenario: After ACK exchange
- **WHEN** initiator and responder completed handshake
- **THEN** both devices report handshake ready

### Requirement: Explicit non-features
MatchManagerV2 SHALL NOT implement: `didWin()`, shootout match priming, chain boost providers, `toJson()` for legacy upload, `LastMatchDisplay`, or mutation of legacy `Player` stats (wins, streak, reaction time averages).

#### Scenario: Surface check
- **WHEN** NodeNetwork links MatchManagerV2
- **THEN** ShootoutManager and ChainDuelManager are not linkage dependencies

### Requirement: Display data stays in states
Reaction times for win/retry UI SHALL be read from session accessors (initiator/responder times and local link side) by states, not from a coordinator-owned display snapshot struct.

#### Scenario: Win state
- **WHEN** win state renders times
- **THEN** it reads from session via coordinator query methods without LastMatchDisplay
