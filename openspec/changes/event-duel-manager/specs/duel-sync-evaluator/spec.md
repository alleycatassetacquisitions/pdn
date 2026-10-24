## ADDED Requirements

### Requirement: Sync evaluation is pure
The module `duel-sync-evaluator` SHALL provide `evaluateDidSync(const DuelCompletionInputs& inputs, unsigned long windowMs)` with no side effects. It SHALL NOT include headers from wireless, storage, or player profile types.

#### Scenario: Inside window
- **WHEN** both initiator and responder draw times are present, exchange is complete, neither never-pressed nor grace-without-local-press applies, and `abs(tInitiator - tResponder) <= windowMs`
- **THEN** `evaluateDidSync` returns true

#### Scenario: Outside window
- **WHEN** both draw times are present, exchange is complete, and `abs(tInitiator - tResponder) > windowMs`
- **THEN** `evaluateDidSync` returns false

#### Scenario: Opponent never pressed
- **WHEN** opponent never pressed is true
- **THEN** `evaluateDidSync` returns false

#### Scenario: Grace without local press
- **WHEN** grace expired without local press is true
- **THEN** `evaluateDidSync` returns false

#### Scenario: Missing draw time
- **WHEN** either initiator or responder draw time is not marked present
- **THEN** `evaluateDidSync` returns false

### Requirement: Default event window
Callers on the Node/event path SHALL use `NODE_SYNC_WINDOW_MS` (250) unless a test overrides the window parameter.

#### Scenario: Event default
- **WHEN** coordinator evaluates sync for NodeNetwork
- **THEN** the window passed to `evaluateDidSync` is 250 ms

### Requirement: Duel completion is separate from sync
The module SHALL provide `duelResultsAreComplete(const DuelCompletionInputs& inputs)` that is true when the duel phase may transition to result UX, including never-pressed and grace paths, without implying sync success.

#### Scenario: Both results received
- **WHEN** local pressed and remote result received
- **THEN** `duelResultsAreComplete` returns true

#### Scenario: Opponent never pressed path
- **WHEN** local pressed and opponent never pressed is true
- **THEN** `duelResultsAreComplete` returns true
- **AND** `evaluateDidSync` returns false

#### Scenario: Grace without local press
- **WHEN** grace expired without local press is true
- **THEN** `duelResultsAreComplete` returns true
- **AND** `evaluateDidSync` returns false

#### Scenario: In progress
- **WHEN** local pressed and remote result not received and grace not expired
- **THEN** `duelResultsAreComplete` returns false

### Requirement: No competitive winner function
This module SHALL NOT expose `didWin` or any fastest-draw comparison.

#### Scenario: API surface
- **WHEN** a consumer includes only `duel-sync-evaluator.hpp`
- **THEN** no symbol named `didWin` is available
