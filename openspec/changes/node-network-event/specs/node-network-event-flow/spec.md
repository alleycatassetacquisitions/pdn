## ADDED Requirements

### Requirement: Jack roles define initiator and responder
Node gameplay SHALL treat the PDN connected to the **OUTPUT** jack as **initiator** and the PDN on the **INPUT** jack as **responder** for connection, duel start, and MatchManagerV2 A/B mapping.

#### Scenario: Output side starts duel
- **WHEN** idle on initiator sees a direct PDN peer on the output jack
- **THEN** initiator may call match initialization
- **AND** responder waits on input jack

### Requirement: Sync success UX for both players
When MatchManagerV2 reports sync success, both devices SHALL enter a success state (e.g. NodeWin). Neither device SHALL enter a competitive lose state for sync scoring.

#### Scenario: Both in window
- **WHEN** both devices finalize with `didSync()` true
- **THEN** both show success UX
- **AND** both persist a record for upload

### Requirement: Sync failure UX without upload
When sync fails, both devices SHALL enter a soft-fail UX (e.g. NodeLinkRetry) and return to idle without entering the upload state and without issuing match upload HTTP.

#### Scenario: Out of window
- **WHEN** duel completes with `didSync()` false
- **THEN** upload state is not entered
- **AND** no HTTP match upload is queued

### Requirement: Upload uses protobuf quickdraw matches
After sync success, NodeNetwork SHALL upload persisted records via `MatchesApi::uploadQuickdrawMatches` and the existing `QuickdrawMatchCodec` contract. Legacy JSON `MatchesApi::updateMatches` SHALL NOT be used on this path.

#### Scenario: Success path upload
- **WHEN** upload state runs with one persisted record
- **THEN** the wireless layer receives `POST /quickdraw-matches` with protobuf body

### Requirement: Out of scope minigames
NodeNetwork state map SHALL NOT include shootout, chain tournament, supporter duel, or symbol-match states. FDN jack symbol match is deferred.

#### Scenario: State map scope
- **WHEN** NodeNetwork populates states
- **THEN** shootout and chain state types are absent
