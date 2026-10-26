## ADDED Requirements

### Requirement: Facade over session, wireless, and storage
`MatchManagerV2` SHALL own or reference one `DuelSession`, one `NodeNetworkWirelessManager`, and one `MatchStorage`, and SHALL be the only duel type Node gameplay states depend on. It SHALL NOT expose a mutable match object, `getCurrentMatch()`, `didWin()`, `toJson()`, or a display-snapshot struct.

#### Scenario: Surface
- **WHEN** a Node state includes `match-manager-v2.hpp`
- **THEN** it has no access to legacy `Match` or Quickdraw packet types

### Requirement: Local identity from PlayerProvider
The manager SHALL read the local participant from `PlayerProvider::getPlayer()` using `getServerPlayerId()` and `getRegistrationTeam()`. Starting a match or finalizing SHALL fail when `hasRegistrationProfile()` is false.

#### Scenario: No registration
- **WHEN** `startMatch` is called without a registration profile
- **THEN** no offer is sent and no session is primed

### Requirement: Link side supplied by the caller
`startMatch` and the inbound handling SHALL take the local link side from the caller (initiator on the OUTPUT jack, responder on the INPUT jack). Only an initiator SHALL send an offer. The manager SHALL NOT infer the side from registration data.

#### Scenario: Responder cannot start
- **WHEN** a responder calls `startMatch`
- **THEN** no offer is sent

### Requirement: Abort on role change
The manager SHALL provide `clearCurrentMatch()` so the caller can end an active duel. It SHALL clear the session without touching storage or the failed-attempt count.

#### Scenario: Jack disconnects
- **WHEN** a state calls `clearCurrentMatch()` during a duel
- **THEN** the session reports no active match
- **AND** `MatchStorage` is unchanged

### Requirement: Local input is reported by states
The manager SHALL expose `onLocalDrawTimeMs(uint64_t)` and `onLocalGraceExpired()`. It SHALL NOT register button callbacks and SHALL NOT apply any button-masher penalty.

#### Scenario: Press
- **WHEN** a state reports 215 ms
- **THEN** the session records the local draw time in the local side's slot
- **AND** a `DRAW` is sent to the opponent

#### Scenario: Grace expiry
- **WHEN** a state reports grace expiry without a press
- **THEN** the session sets grace-without-press and a `NEVER_PRESSED` is sent

### Requirement: Exchange status
The manager SHALL expose `isHandshakeComplete()` and `isExchangeComplete()` mirroring the session so states can transition from idle to countdown and from duel to result.

#### Scenario: After handshake
- **WHEN** offer and accept have been exchanged
- **THEN** both devices report `isHandshakeComplete()` true

### Requirement: Sync decision
`didSync()` SHALL be true only when the exchange is complete, both draw times are present, neither opponent-never-pressed nor grace-without-press is set, and `abs(initiatorTime - responderTime) <= NODE_SYNC_WINDOW_MS` (250). The decision SHALL be symmetric: both devices holding the same two times SHALL reach the same result.

#### Scenario: Inside window
- **WHEN** times are 200 and 350
- **THEN** `didSync()` is true

#### Scenario: Edge
- **WHEN** times differ by exactly 250
- **THEN** `didSync()` is true

#### Scenario: Outside window
- **WHEN** times are 200 and 500
- **THEN** `didSync()` is false

#### Scenario: Never pressed
- **WHEN** opponent-never-pressed is set
- **THEN** `didSync()` is false

### Requirement: Finalize only on sync
`finalizeMatch()` SHALL return true only when a registration profile exists, `didSync()` is true, and the session builds an upload record. It SHALL append that record to `MatchStorage`, clear the session, and return true. On any failure it SHALL NOT change storage.

#### Scenario: Sync failure
- **WHEN** `didSync()` is false
- **THEN** `finalizeMatch()` returns false and the stored count is unchanged

#### Scenario: Success
- **WHEN** the duel synced and the opponent ids are known
- **THEN** `finalizeMatch()` returns true and the stored count increases by one
- **AND** the session reports no active match

#### Scenario: Both devices agree
- **WHEN** both devices of a synced duel finalize
- **THEN** their stored records are equal field-for-field

### Requirement: Failed-attempt limit
The manager SHALL count failed duels per opponent MAC via `recordLinkOutcome(bool syncSucceeded)`. A failure increments the count; a success or a different opponent MAC resets it. `hasExhaustedAttempts()` SHALL be true when the count reaches `MAX_FAILED_ATTEMPTS` (3). The count SHALL survive `clearCurrentMatch()` and SHALL NOT be written to any stored record.

#### Scenario: Third failure
- **WHEN** three failures are recorded for the same peer
- **THEN** `hasExhaustedAttempts()` is true

#### Scenario: Reset on success
- **WHEN** two failures then a success are recorded
- **THEN** the count is zero and `hasExhaustedAttempts()` is false

#### Scenario: Peer change
- **WHEN** two failures are recorded for peer X and a failure for peer Y
- **THEN** the count is one

### Requirement: Storage access for upload
The manager SHALL expose pending-record count, read by index, and clear, for the upload state. It SHALL NOT perform HTTP.

#### Scenario: Upload state
- **WHEN** an upload state reads the manager
- **THEN** it obtains records without access to the session

### Requirement: Explicit non-features
The manager SHALL NOT implement shootout priming, chain boost, supporter logic, competitive win/loss, or mutation of legacy `Player` duel stats (wins, losses, streak, reaction averages).

#### Scenario: Linkage
- **WHEN** NodeNetwork links the manager
- **THEN** `ShootoutManager` and `ChainDuelManager` are not linkage dependencies
