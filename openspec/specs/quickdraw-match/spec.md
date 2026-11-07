# quickdraw-match Specification

## Purpose

When two PDNs are paired for a 1v1 contest — either by cabling a Hunter
challenger and a Bounty challenger directly across their opponent jacks, or by
being paired in a Shootout round — they run a synchronized reaction-time match:
establishing a shared match identity, counting down, measuring each player's
draw time (with any early-press penalties and posse supporter boosts applied),
exchanging results over the wireless channel, and determining the winner and
loser.

This specification states the required outcomes, invariants, and environmental
constraints of a Quickdraw match, independent of internal classes or data
structures.

Terms used throughout:

- **Match**: a single 1v1 reaction-time contest between two paired players.
- **Standalone match**: a match initiated over a physical cable between a Hunter
  challenger and a Bounty challenger outside of a closed ring.
- **Showdown**: a same-role 1v1 match (Hunter vs Hunter or Bounty vs Bounty),
  which standalone match initiation rejects with a role-mismatch reply.
- **Shootout match**: a match primed by a Shootout tournament coordinator
  between two paired players on a closed ring.
- **Standing role**: a player's persistent Hunter or Bounty role from
  registration.
- **Match draw slot**: the Hunter slot or Bounty slot assigned to a player for
  the duration of a single match. In a standalone match, each player's match
  draw slot equals their standing role; in a shootout match, the paired player
  with the lower MAC address takes the Hunter draw slot and the higher MAC takes
  the Bounty draw slot without altering either player's standing role.
- **Raw reaction time**: the elapsed monotonic milliseconds from the start of
  the local draw window to the player's button press, plus any early-press
  countdown penalties.
- **Effective draw time**: the player's raw reaction time minus any posse boost
  earned from confirmed supporters (floored at `0 ms`).

## Requirements

### Requirement: Fault model

The match protocol SHALL remain correct under the following conditions, and no
requirement in this spec may assume otherwise:

- Any wireless match frame (match initiation, match acknowledgement, role
  mismatch rejection, draw result, or never-pressed report) can be lost,
  delayed, or duplicated.
- Frames from a previous match can arrive after a new match has already been
  primed or started.
- A physical cable connecting the two players can be pulled during the handshake,
  during the countdown, or during the draw window.
- Each device measures reaction time against its own monotonic millisecond clock
  from the moment its local draw window opens.
- Either player, both players, or neither player may press their button before
  the draw window expires.

#### Scenario: Stale result from a prior match is ignored
- **WHEN** a player is in an active match with ID $M_2$ and receives a delayed draw result or never-pressed report carrying an earlier match ID $M_1 \ne M_2$
- **THEN** the player ignores the frame and its active match $M_2$ continues unaffected

#### Scenario: Match command from a non-opponent is ignored
- **WHEN** a player in an active match with opponent $B$ receives a match acknowledgement, draw result, or never-pressed report from a sender $C \ne B$
- **THEN** the player ignores the frame

### Requirement: Match initiation and identity

At most one match SHALL be active on a device at any time. Every match SHALL
have a match ID shared by both paired players and a local match draw slot
(Hunter slot or Bounty slot) fixed when the match is primed:

- **Standalone match initiation**:
  - Only a Hunter challenger that is idle, not on a ring, and has a Connected
    Bounty PDN on its downstream jack SHALL initiate a standalone match, minting
    a fresh UUID match ID and sending a match-initiation frame (carrying the
    match ID, the Hunter's player ID, and the Hunter role) to that Bounty peer.
  - A device receiving a match-initiation frame SHALL ignore it unless the
    sender is a currently cabled direct jack neighbor and no match is already
    active.
  - If the receiver is not in a shootout and has the same standing role as the
    sender (an attempted same-role showdown), the receiver SHALL reject the
    initiation by replying with a role-mismatch frame carrying that match ID;
    the initiator receiving a role-mismatch reply for its active match SHALL
    clear the pending match.
  - Otherwise, the receiver SHALL prime its active match with the received match
    ID, record the opponent's player ID in the opponent's draw slot, reply with
    a match-acknowledgement frame carrying the match ID and its own player ID,
    and mark the match ready to enter the countdown.
  - When the initiating Hunter receives the match acknowledgement from its
    active opponent with the matching match ID, it SHALL record the Bounty's
    player ID, stop retransmitting the match initiation, and mark the match
    ready to enter the countdown.
  - If the initiator does not receive the match acknowledgement before the match
    initialization timeout expires, it SHALL clear the pending match so a fresh
    initiation can occur if the cable remains connected.
- **Shootout match priming**:
  - When a shootout match-start command pairs two players, each paired player
    primes its active match locally using the deterministic match ID derived
    from the tournament match identity and assigns the Hunter draw slot to the
    lower MAC address and the Bounty draw slot to the higher MAC address,
    without exchanging a standalone match handshake and without mutating either
    player's standing role.

#### Scenario: Standalone handshake completes and enters countdown
- **WHEN** Hunter $H$ sends a match initiation with ID $M$ to cabled Bounty $B$ and $B$ replies with a match acknowledgement for ID $M$
- **THEN** both $H$ and $B$ record match $M$ with each other's player IDs and transition from idle into the match countdown

#### Scenario: Same-role showdown initiation is rejected with role mismatch
- **WHEN** a device receives a match initiation from a direct neighbor whose role equals the receiver's own standing role outside of a shootout
- **THEN** the receiver does not prime a match and replies with a role-mismatch rejection, causing the initiator to clear its pending match

#### Scenario: Shootout match preserves standing role
- **WHEN** two standing Bounties $B_1$ and $B_2$ ($\text{MAC}(B_1) < \text{MAC}(B_2)$) are paired in a shootout match
- **THEN** $B_1$ is assigned the Hunter draw slot and $B_2$ the Bounty draw slot for that match, and both players' standing roles remain Bounty during and after the match

### Requirement: Countdown, early-press penalty, and posse notifications

On entering the match countdown, the device SHALL run a 3-step visual and haptic
countdown (3, 2, 1, at 2 seconds per step, 6 seconds total) before opening the
draw window:

- On countdown entry, a posse challenger SHALL broadcast a countdown posse event
  to its posse supporters to arm their confirmation window and clear
  confirmations from any prior round.
- Every button press by a paired player during the countdown (before the draw
  window opens) SHALL increment that player's early-press counter, adding a
  **75 ms penalty per early press** to that player's subsequent reaction time in
  the draw window.
- On exiting the countdown (whether advancing into the draw window or aborting),
  a posse challenger SHALL broadcast a draw posse event so supporters do not
  remain stuck on the armed prompt.

#### Scenario: Pressing twice during countdown adds 150 ms penalty
- **WHEN** a player presses the button twice during the countdown and then presses 200 ms after the draw window opens with no posse boost
- **THEN** the player's raw and effective draw time is $200 + 2 \times 75 = 350\text{ ms}$

### Requirement: Draw window, posse boost application, and result exchange

When the countdown completes, the device SHALL open the active draw window for
up to **4000 ms**, record the local monotonic start timestamp, and (if a posse
challenger) broadcast a draw posse event to disarm supporter confirmations:

- **First button press during the draw window (or post-receive grace window)**:
  - Only the first button press in the draw window is counted; subsequent presses
    in the same match are ignored.
  - The device computes the player's raw reaction time (elapsed milliseconds
    since the draw window opened plus $75\text{ ms}$ per early countdown press)
    and effective draw time (raw reaction time minus posse supporter boost,
    floored at $0\text{ ms}$).
  - The device records the raw reaction time in the player's personal
    reaction-time history, records the effective draw time in its match draw
    slot (Hunter slot or Bounty slot), snapshots the posse boost for the result
    screen, and reliably sends a draw-result frame carrying its effective draw
    time and its match draw slot to the opponent.
- **Pressing before receiving the opponent's result (post-push wait)**:
  - If the local player presses before receiving a draw result or never-pressed
    report from the opponent, the device waits up to a **900 ms** result grace
    period for the opponent's result to arrive, then transitions to match
    resolution as soon as the opponent's result arrives or the 900 ms grace
    period expires.
- **Receiving the opponent's result before pressing (post-receive wait)**:
  - If the device receives the opponent's draw result or never-pressed report
    while in the draw window before pressing locally, it records the opponent's
    draw time and enters a **750 ms** button-push grace window during which the
    local player can still press.
  - If the local player presses within that 750 ms grace window, the device
    sends its draw result and immediately resolves the match.
  - If the 750 ms grace window expires without a local press, the device records
    a pity draw time (elapsed time since draw window start), marks itself as
    never having pressed, reliably sends a never-pressed report to the opponent,
    and resolves the match as a loss.

#### Scenario: Posse boost reduces effective draw time sent to opponent
- **WHEN** a Hunter challenger with 2 confirmed supporters ($30\text{ ms}$ boost) presses 210 ms after the draw window opens with 0 early presses, and the Bounty opponent presses at 195 ms with 0 boost
- **THEN** the Hunter records a personal reaction time of 210 ms, sends an effective draw time of $210 - 30 = 180\text{ ms}$ in its draw result, and wins the match ($180 < 195$)

#### Scenario: Opponent presses first and local player fails to press within 750 ms grace
- **WHEN** player $A$ receives a draw result of 220 ms from player $B$ and does not press within the 750 ms grace window
- **THEN** $A$ sends a never-pressed report to $B$, $A$ resolves the match as a loss, and $B$ resolves the match as a win

### Requirement: Winner determination and timeout divergence

When a match resolves, the outcome SHALL be determined as follows:

1. **Opponent never pressed**: If the local player pressed and received a
   never-pressed report from the opponent, the local player **wins**.
2. **Local player never pressed**: If the local player's 750 ms post-receive
   grace period expired without a local press, the local player **loses**.
3. **Both players pressed**: The local player **wins** if and only if both draw
   times are recorded and the local player's effective draw time is strictly
   less than the opponent's effective draw time; otherwise (including on an
   exact millisecond tie or if the opponent's result never arrived before the
   900 ms post-push grace period expired) the local player **loses**.
4. **Full 4000 ms draw-window timeout (neither player pressed and no result
   received)**:
   - **Standalone match**: Both devices abandon the match without recording a
     win, loss, or match played, and return to idle.
   - **Shootout match**: The player holding the **Hunter match draw slot**
     forfeits (transitions to shootout eliminated), and the player holding the
     **Bounty match draw slot** wins (reports the match win to the tournament
     and transitions to shootout spectator).

If a button press occurs in the same tick that a grace period expires, the
button press SHALL take precedence over the timeout.

#### Scenario: Standalone 4-second timeout returns both players to idle without stats
- **WHEN** the 4000 ms draw window expires in a standalone match with neither player pressing
- **THEN** both devices clear the match and return to idle without incrementing matches played, wins, or losses

#### Scenario: Shootout 4-second timeout eliminates the match's Hunter slot
- **WHEN** the 4000 ms draw window expires in a shootout match with neither player pressing
- **THEN** the player in the Hunter draw slot is eliminated and the player in the Bounty draw slot reports the win and advances

### Requirement: Cable disconnect and match abandonment

In a standalone match (when no shootout tournament is active), if the player's
required opponent jack (downstream jack for a standing Hunter, upstream jack for
a standing Bounty) remains disconnected past the disconnect debounce during the
countdown, the draw window, the post-push wait, or the post-receive wait, the
device SHALL abandon the match, clear the active match state, and return to
idle.

While a shootout tournament is active, single-jack disconnect debounce on the
match states SHALL NOT return the device to idle; tournament-wide ring-break
handling owns aborting the shootout.

#### Scenario: Unplugging cable during standalone countdown cancels the match
- **WHEN** the cable between a Hunter and a Bounty is unplugged during the standalone match countdown and stays disconnected past the debounce window
- **THEN** both devices clear the match and return to idle

### Requirement: Post-match statistics, persistence, and result display

When a match resolves with a win or loss outcome:

- The device SHALL increment the player's total matches played, and either
  increment wins and win streak (on a win) or increment losses and reset win
  streak to 0 (on a loss).
- **Standalone match**:
  - The finalized match record (match ID, hunter ID, bounty ID, hunter draw
    time, bounty draw time) SHALL be appended to non-volatile match storage (up
    to 255 stored matches) for subsequent server upload.
  - The device SHALL display the win or loss result screen for **8000 ms**,
    showing the player's effective draw time, the opponent's effective draw
    time, and the number of posse supporter boosts applied, while broadcasting
    a posse win or loss event to its posse supporters.
  - After 8000 ms on the win or loss screen, the device SHALL transition to
    match upload (disabling the peer-to-peer radio to upload stored matches over
    Wi-Fi, retrying up to 3 times before timing out, and clearing storage on
    upload success), then pass through the dormant sleep and awaken screens
    before re-enabling the peer-to-peer radio and returning to idle.
- **Shootout match**:
  - The match SHALL NOT be written to non-volatile match storage and SHALL NOT
    trigger match upload or sleep; the winner reports the match result to the
    tournament and enters the between-matches spectator screen, and the loser
    enters the eliminated screen.

#### Scenario: Standalone win saves match, notifies posse, uploads, and sleeps
- **WHEN** a challenger wins a standalone match
- **THEN** the match is saved to non-volatile storage, a posse win event is broadcast to its posse supporters, the win screen displays for 8 seconds, and the device proceeds through match upload and sleep before re-enabling the peer-to-peer radio and returning to idle

#### Scenario: Shootout match skips storage and upload
- **WHEN** a paired player wins or loses a shootout match
- **THEN** no match record is written to non-volatile storage and the player transitions directly to the shootout spectator or eliminated state
