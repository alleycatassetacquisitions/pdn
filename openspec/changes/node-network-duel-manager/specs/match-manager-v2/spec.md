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
The manager SHALL provide `clearCurrentMatch()` so the caller can end an active duel. It SHALL clear the session without touching storage, the failed-attempt count, or the abort flag.

#### Scenario: Jack disconnects
- **WHEN** a state calls `clearCurrentMatch()` during a duel
- **THEN** the session reports no active match
- **AND** `MatchStorage` is unchanged

### Requirement: Clock sync on uptime
Round timing SHALL use each device's uptime clock, never unix time. After the handshake the initiator SHALL send `NODE_SYNC_PROBE_COUNT` (5) `SYNC` probes, `NODE_SYNC_PROBE_INTERVAL_MS` (50) apart, each stamped with its uptime T1. The responder SHALL reply `SYNC_REPLY` with T1, its uptime on arrival T2, and its uptime on sending T3. With arrival time T4 the initiator SHALL compute offset = ((T2−T1)+(T3−T4))/2 and round trip = (T4−T1)−(T3−T2) in wrapping 32-bit math, drop a reply whose hold T3−T2 exceeds T4−T1, and keep the sample with the smallest round trip. `hasClockSync()` SHALL be true when that round trip is at most `NODE_SYNC_MAX_RTT_MS` (75, a compile-time constant). Replies after the sequence is set SHALL be ignored.

#### Scenario: Offset measured
- **WHEN** the responder's uptime is 776777 ms ahead and the link is symmetric
- **THEN** the kept offset is 776777

#### Scenario: Slow link
- **WHEN** every sample's round trip exceeds 75 ms
- **THEN** `hasClockSync()` is false and the round is aborted

### Requirement: Initiator shares one schedule
The initiator SHALL generate a `NodeInputSequence` of `NODE_SEQUENCE_LENGTH` (5) steps, each drawn independently: with probability 2/6 one random player gets a one-button prompt, with 3/6 one random player gets a two-button prompt, and with 1/6 both players press, each getting one or two buttons with equal chance (the counts may differ). Every button is `ButtonIdentifier::PRIMARY_BUTTON` or `SECONDARY_BUTTON` at random. `beginRound(sequence)` SHALL require the handshake and a clock sync, set step 0 to `NODE_COUNTDOWN_MS` (6000) from now on the initiator's uptime, and send one reliable `SEQUENCE` carrying the step prompts and step 0 converted to the responder's uptime with the sync offset. A step both players press SHALL use `NODE_BOTH_INPUT_WINDOW_MS` (3000) and `NODE_BOTH_STEP_GAP_MS` (3300); a one-player step with two buttons SHALL use `NODE_DOUBLE_INPUT_WINDOW_MS` (2100) and `NODE_DOUBLE_STEP_GAP_MS` (2400); other steps use `NODE_INPUT_WINDOW_MS` (1400) and `NODE_STEP_GAP_MS` (1700). Step i SHALL start after step 0 by the sum of the earlier steps' windows and gaps on both devices. All steps SHALL be played regardless of earlier results. There are no per-step prompt messages.

#### Scenario: Responder adopts the schedule
- **WHEN** the responder receives `SEQUENCE` with step 0 4000 ms ahead of its uptime
- **THEN** its session holds the same prompts as the initiator
- **AND** `msUntilStep(0)` is 4000 on the responder

#### Scenario: Invalid prompts
- **WHEN** a `SEQUENCE` has a step with a button count above 2 or no pressing side
- **THEN** the responder aborts the round

#### Scenario: Implausible lead
- **WHEN** a `SEQUENCE`'s step 0 is in the past or more than `NODE_COUNTDOWN_MS` + `NODE_SEQUENCE_LEAD_SLACK_MS` (1000) away
- **THEN** the responder aborts the round

### Requirement: Presser judges locally
The manager SHALL expose `onLocalInputResult(step, success, pressCount)`. It SHALL accept a result only for a step the local player presses and only once per step, record it as the local side's part, and send it as a reliable `INPUT_RESULT` from either side. `onLocalPress(step, pressCount)` SHALL send an unreliable `INPUT_PRESS`. The manager SHALL NOT register button callbacks; states SHALL judge presses within the step's window, in prompt order: a wrong button ends the local part as a failure at once, the last correct button ends it as a success, and an unfinished prompt fails when the window closes. Each device SHALL accept `INPUT_RESULT` and `INPUT_PRESS` only for steps the opponent presses, keeping the highest reported press count.

#### Scenario: Responder press
- **WHEN** the responder reports success for step 1
- **THEN** a reliable `INPUT_RESULT` with step 1 and success 1 is sent
- **AND** a second report for step 1 is rejected

#### Scenario: Both-player step
- **WHEN** the initiator's part of a both-player step succeeds and the responder's fails
- **THEN** the step fails

#### Scenario: Viewer cannot report
- **WHEN** a device reports a result for a step whose presser is the other device
- **THEN** the call returns false and nothing is sent

### Requirement: Initiator resolves the outcome
`expireStep(step)` SHALL fail every pending part of a step with no result. The initiator state SHALL call `finishRound()` once every step is resolved or `NODE_RESULT_GRACE_MS` (1000) after the last window. `finishRound()` SHALL fail every unresolved step, fix the per-side success counts as the outcome, and send a reliable `OUTCOME`. The session SHALL record whether `OUTCOME` was acknowledged or ran out of resends. The initiator state SHALL conclude the round only once `OUTCOME` is acknowledged, and SHALL abort it uncounted when `OUTCOME` runs out of resends.

#### Scenario: Lost result
- **WHEN** the responder's `INPUT_RESULT` for a step is never acknowledged
- **THEN** the initiator counts that step as failed

#### Scenario: Lost outcome
- **WHEN** the first `OUTCOME` send is lost
- **THEN** a resend reaches the responder, the initiator concludes on its `ACK`, and both devices hold the same score

#### Scenario: Outcome never acknowledged
- **WHEN** no `ACK` for `OUTCOME` arrives within eight sends
- **THEN** the initiator aborts and counts no attempt

### Requirement: Responder adopts the outcome
The responder SHALL accept `OUTCOME` only from the opponent of its active session and only when the two scores sum to at most `NODE_SEQUENCE_LENGTH`. If no outcome arrives within `NODE_OUTCOME_WAIT_MS` (2500) after the last window, the responder SHALL abort the round uncounted. `NODE_OUTCOME_WAIT_MS` SHALL exceed `NODE_RESULT_GRACE_MS` plus every `OUTCOME` resend, so the responder never gives up while a copy it would acknowledge is still coming.

#### Scenario: Outcome never arrives
- **WHEN** no `OUTCOME` arrives, even though the initiator acknowledged earlier packets
- **THEN** the round is aborted and no attempt is counted

### Requirement: Uncounted abort
`abortRound()` SHALL send a reliable `ABORT` when a session is active, raise the abort flag, and clear the session without touching storage or the failed-attempt count. A valid `ABORT` from the opponent SHALL do the same without replying. `isAborted()` SHALL stay true until `clearAbort()`. States SHALL abort when the clock sync fails, the `SEQUENCE` goes unacknowledged by step 0 or runs out of resends, the responder gets no `SEQUENCE` within `NODE_SEQUENCE_WAIT_MS` (2000), the session ends mid-round, `OUTCOME` is never acknowledged, or the responder gets no `OUTCOME`. An aborted round SHALL show "Error / Retrying" and return to idle.

#### Scenario: Abort reaches opponent
- **WHEN** one device aborts mid-round
- **THEN** both devices report `isAborted()` and neither counts a failed attempt

### Requirement: Round status
The manager SHALL expose `isHandshakeComplete()` and `isOutcomeKnown()` so states can transition from idle to countdown and from duel to score. A round SHALL succeed when the outcome is known and the combined score is at least `NODE_SUCCESS_THRESHOLD` (4). Both devices holding the same outcome SHALL reach the same result.

#### Scenario: After handshake
- **WHEN** offer and accept have been exchanged
- **THEN** both devices report `isHandshakeComplete()` true

#### Scenario: Four of five
- **WHEN** the outcome holds 4 successful steps
- **THEN** both devices treat the round as a success

#### Scenario: Three of five
- **WHEN** the outcome holds 3 successful steps
- **THEN** both devices treat the round as a failure

### Requirement: Conclude round
`concludeRound()` SHALL store the record on success via `finalizeMatch()`, count the attempt with `recordLinkOutcome`, save the score and success for the score screen (`getLastRoundScore()`, `wasLastRoundSuccess()`), and clear the session. Without a known outcome the score SHALL be the local best guess and the round SHALL fail.

#### Scenario: Score screen
- **WHEN** a round concludes with 3 successful steps
- **THEN** `getLastRoundScore()` is 3 and `wasLastRoundSuccess()` is false

### Requirement: Finalize only on success
`finalizeMatch()` SHALL return true only when a registration profile exists, the round succeeded, and the session builds an upload record. It SHALL append that record to `MatchStorage`, clear the session, and return true. On any failure it SHALL NOT change storage.

#### Scenario: Round failure
- **WHEN** the round did not succeed
- **THEN** `finalizeMatch()` returns false and the stored count is unchanged

#### Scenario: Success
- **WHEN** the round succeeded and the opponent ids are known
- **THEN** `finalizeMatch()` returns true and the stored count increases by one
- **AND** the session reports no active match

#### Scenario: Both devices agree
- **WHEN** both devices of a successful round finalize
- **THEN** their stored records are equal field-for-field

### Requirement: Failed-attempt limit
The manager SHALL count failed duels per opponent MAC via `recordLinkOutcome(bool success)`. A failure increments the count; a success or a different opponent MAC resets it. `hasExhaustedAttempts()` SHALL be true when the count reaches `MAX_FAILED_ATTEMPTS` (3). The count SHALL survive `clearCurrentMatch()` and SHALL NOT be written to any stored record.

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
