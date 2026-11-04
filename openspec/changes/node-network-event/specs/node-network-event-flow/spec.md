## ADDED Requirements

### Requirement: Jack roles define initiator and responder
Node gameplay SHALL treat the PDN connected to the **OUTPUT** jack as **initiator** and the PDN on the **INPUT** jack as **responder** for connection, duel start, and MatchManagerV2 A/B mapping.

#### Scenario: Output side starts duel
- **WHEN** idle on initiator sees a direct PDN peer on the output jack
- **THEN** initiator may call match initialization
- **AND** responder waits on input jack

### Requirement: Idle stats pages
Node idle SHALL expose exactly **two** stats pages cycled by the primary or secondary button, reusing the Quickdraw idle **Alleycat** frame image layout.

#### Scenario: Nodes created page
- **WHEN** stats page index is 0 (default)
- **THEN** the display shows multiline label `Nodes` / `Created` and the count of successful unique partner connections on the next line

#### Scenario: Symbol display page
- **WHEN** stats page index is 1
- **THEN** the display shows the local player's symbol glyph on the idle frame
- **AND** paging to this page does **not** enter symbol match gameplay

### Requirement: Nodes created counter
The device SHALL persist a **Nodes Created** count. It SHALL increment by one only when **both** are true: a duel record upload via `POST /quickdraw-matches` succeeds, and the opponent peer **MAC** had not previously been counted after a successful upload.

#### Scenario: First success with a peer
- **WHEN** upload succeeds for a peer MAC never counted before
- **THEN** Nodes Created increments by one
- **AND** that MAC is remembered as counted

#### Scenario: Repeat peer
- **WHEN** upload succeeds for a peer MAC already counted
- **THEN** Nodes Created does not increment

### Requirement: Symbol match entry from FDN jack only
Symbol match gameplay SHALL begin only when the device detects an **FDN** on a jack while in Node idle (same detection rule as Quickdraw idle). The symbol stats page SHALL NOT trigger symbol entry.

#### Scenario: FDN connected
- **WHEN** idle detects FDN on output or input jack
- **THEN** NodeNetwork enters symbol match states

#### Scenario: Stats page only
- **WHEN** the player cycles to the symbol stats page without FDN connected
- **THEN** the device remains in Node idle

### Requirement: Sync success UX for both players
When MatchManagerV2 reports sync success, both devices SHALL enter a success state (`NodeWin`). Neither device SHALL enter a competitive lose state for sync scoring.

#### Scenario: Both in window
- **WHEN** both devices finalize with `didSync()` true
- **THEN** both show success UX
- **AND** both persist a record for upload

### Requirement: Sync failure UX without upload
When sync fails, both devices SHALL enter `NodeLinkRetry` showing retries remaining, then return to idle without upload. Upload state SHALL NOT run.

#### Scenario: Out of window
- **WHEN** duel completes with `didSync()` false
- **THEN** upload state is not entered
- **AND** no HTTP match upload is queued

#### Scenario: Retries display
- **WHEN** `NodeLinkRetry` is shown after a failed duel
- **THEN** the display reuses the idle stats layout with multiline `Retries` / `Left` / `#`
- **AND** `#` equals remaining attempts before sleep (`MAX_FAILED_ATTEMPTS - failedAttempts` after the failure is recorded)

#### Scenario: Third failure shows zero then sleep
- **WHEN** the third consecutive failed duel with the same peer completes
- **THEN** `NodeLinkRetry` shows `0` retries left
- **AND** the device transitions to `NodeSleep` (not idle)

### Requirement: Sleep after success and after exhausted failures
After a successful match upload path completes (including upload retries exhausted per upload state policy), the device SHALL enter `NodeSleep`, then the awaken sequence, then Node idle. After exhausted link failures, the device SHALL follow the same sleep → awaken → idle path.

#### Scenario: Success path
- **WHEN** upload completes successfully or upload state gives up after retries
- **THEN** the device enters sleep, then awaken, then idle

#### Scenario: Failure path
- **WHEN** link retries are exhausted
- **THEN** the device enters sleep, then awaken, then idle

### Requirement: Sleep clears link failure count
When `NodeSleep` mounts, MatchManagerV2 SHALL clear the failed-attempt counter for the current tracked peer MAC.

#### Scenario: Enter sleep
- **WHEN** sleep state mounts
- **THEN** `getFailedAttempts()` for the next duel with that peer MAC starts at zero

### Requirement: Upload uses protobuf quickdraw matches
After sync success, NodeNetwork SHALL upload persisted records via `MatchesApi::uploadQuickdrawMatches` and the existing `QuickdrawMatchCodec` contract. Legacy JSON `MatchesApi::updateMatches` SHALL NOT be used on this path.

#### Scenario: Success path upload
- **WHEN** upload state runs with one persisted record
- **THEN** the wireless layer receives `POST /quickdraw-matches` with protobuf body

### Requirement: Alleycat UI assets
Node gameplay states SHALL use the same Quickdraw image set as **`Allegiance::ALLEYCAT`** via `getImageForAllegiance` (countdown, duel, win, idle frame, sleep logos, awaken stamp).

#### Scenario: Event artwork
- **WHEN** any Node gameplay state renders bitmap UI
- **THEN** it uses the Alleycat image table, not hunter/bounty variants

### Requirement: In-scope minigames
NodeNetwork SHALL include symbol match states (`NodeSymbol`, `NodeSymbolMatched` or equivalent Quickdraw port) reachable from idle on FDN jack detection. Shootout, chain tournament, and supporter duel states SHALL remain absent.

#### Scenario: State map scope
- **WHEN** NodeNetwork populates states
- **THEN** shootout and chain state types are absent
- **AND** symbol match states are present
