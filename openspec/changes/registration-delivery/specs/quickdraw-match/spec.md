## ADDED Requirements

### Requirement: Match secret over the cable

For each standalone match the Hunter SHALL draw a 16-byte secret from the hardware RNG and queue it with the 16-byte match UUID as one application frame on its opponent jack before the initiation; if the jack refuses it, no match starts. A Bounty SHALL take a secret only from its opponent jack while no match is active, and SHALL prime a match only from an initiation by its opponent jack's peer that verifies under that match's secret, holding the latest one unverified until then.

#### Scenario: Initiation overtakes its secret
- **WHEN** a Bounty receives Hunter $H$'s match initiation for match $M$ before $M$'s secret arrives on its opponent jack
- **THEN** it primes nothing and sends nothing until the secret arrives, then primes $M$ and acknowledges it

#### Scenario: Initiation without its secret primes nothing
- **WHEN** a Bounty holds no secret for match $M$, holds one for another match, received $M$'s secret on its supporter jack, or receives an initiation for $M$ whose tag was made under another secret
- **THEN** it primes no match and sends no acknowledgement

### Requirement: Signed standalone match frames

Standalone match initiation, acknowledgement, draw-result and never-pressed frames SHALL carry a rising counter and a tag: the first 16 bytes of HMAC-SHA-256 under the match secret over all fields but the sequence number. A receiver SHALL check the last three for sender, match, draw slot, tag and counter instead of duplicate detection; a failing frame changes nothing and no unsigned frame takes a signed one's sequence number. Shootout, role-mismatch, posse, roster and join frames stay unsigned.

#### Scenario: Forged draw result does not block the honest one
- **WHEN** a radio attacker spoofing Hunter $H$'s MAC sends a $0\text{ ms}$ draw result with a $270\text{ ms}$ boost under $H$'s next sequence number, and $H$'s own draw result follows under the same sequence number
- **THEN** the Bounty refuses the forgery and applies $H$'s result

#### Scenario: Unsigned frame does not block the signed result
- **WHEN** a radio attacker spoofing Hunter $H$'s MAC sends a role-mismatch frame for the running match, or a frame naming a shootout match, under $H$'s next sequence number, and $H$'s own draw result follows under the same sequence number
- **THEN** the Bounty keeps the match and applies $H$'s result

#### Scenario: Forged never-pressed report is refused
- **WHEN** a radio attacker spoofing the opponent's MAC sends a never-pressed report for the active standalone match without the match secret
- **THEN** the receiver does not record the opponent as never having pressed

#### Scenario: Replayed frame is refused
- **WHEN** a genuine draw result is sent again under a fresh sequence number after a later frame from the same sender was accepted
- **THEN** the receiver refuses it and keeps the later result

#### Scenario: Shootout frames stay unsigned
- **WHEN** a shootout match exchanges draw results
- **THEN** they carry no tag and are applied as soon as they arrive

### Requirement: Early opponent result hold

A standalone match SHALL hold a verified draw result or never-pressed report that arrives before the local draw window opens, keeping only the latest, and SHALL apply it when the window opens; it SHALL NOT refuse it as early. A shootout match applies results on arrival.

#### Scenario: Early result applies when the window opens
- **WHEN** a Bounty receives the Hunter's signed draw results of $300\text{ ms}$ and then $260\text{ ms}$ before its own draw window opens
- **THEN** neither counts until the window opens, and then the opponent's draw time is $260\text{ ms}$

## MODIFIED Requirements

### Requirement: Draw window, posse boost application, and result exchange

When the countdown completes, the device SHALL open the active draw window for up to **4000 ms** and record the local monotonic start timestamp:

- **First button down-edge press during the draw window (or post-receive grace window)**:
  - Both the primary and secondary buttons SHALL score on the button down-edge (`PRESS`, not button release/click resolution) so grip hold duration does not inflate reaction time; only the first button down-edge press in the draw window is counted and subsequent presses in the same match are ignored.
  - The device computes the player's raw reaction time (elapsed milliseconds since the draw window opened plus $75\text{ ms}$ per early countdown press) and effective draw time (raw reaction time minus posse supporter boost, with no floor).
  - The device records the raw reaction time in the player's personal reaction-time history and in its match draw slot (Hunter slot or Bounty slot) on the match record, snapshots the posse boost and early-press penalty, uses the effective draw time for winner determination and the result screen, and reliably sends a draw-result frame carrying its raw reaction time and its posse boost alongside its match draw slot to the opponent. The boost it applies and sends is at most a full posse's worth (18 supporters, $270\text{ ms}$).
- **Validating an incoming draw result from the opponent**:
  - The device SHALL accept a draw-result frame only if it comes from the active opponent for the active match ID, names the opponent's match draw slot (never the local player's draw slot), carries a raw reaction time of $0\text{ ms}$ or more and, in a standalone match, verifies as a signed frame. There is no reaction-time floor: $0\text{ ms}$ counts. A standalone result that arrives before the draw window opens is held until it opens. It records the opponent's raw reaction time in the opponent's match draw slot on the match record, and takes the opponent's effective draw time for winner determination and the result screen as that raw time minus the frame's boost.
  - The device SHALL hold a received boost to a full posse's worth: a frame claiming more than $270\text{ ms}$ is treated as claiming $270\text{ ms}$.
- **Pressing before receiving the opponent's result (post-push wait)**:
  - If the local player presses before receiving a draw result or never-pressed report from the opponent, the device waits up to a **2500 ms** result grace period (`750 ms` opponent button-push grace window plus the `1500 ms` reliable-channel retry deadline plus a `250 ms` margin) for the opponent's result to arrive, then transitions to match resolution as soon as the opponent's result arrives or the 2500 ms grace period expires.
- **Receiving the opponent's result before pressing (post-receive wait)**:
  - If the device receives the opponent's draw result or never-pressed report while in the draw window before pressing locally, it records the opponent's draw time and enters a **750 ms** button-push grace window during which the local player can still press (scored on the button down-edge `PRESS`).
  - If the local player presses within that 750 ms grace window, the device sends its draw result and immediately resolves the match.
  - If the 750 ms grace window expires without a local press, the device records a pity draw time (elapsed time since draw window start), marks itself as never having pressed, reliably sends a never-pressed report to the opponent, and resolves the match as a loss.

#### Scenario: Posse boost reduces effective draw time for winner determination while preserving raw reaction times on the match record
- **WHEN** a Hunter challenger with 2 confirmed supporters ($30\text{ ms}$ boost) presses 210 ms after the draw window opens with 0 early presses, and the Bounty opponent presses at 195 ms with 0 boost
- **THEN** the Hunter records a personal and match-record raw reaction time of 210 ms, sends its 210 ms raw reaction time and 30 ms boost in its draw result, and wins the match ($210 - 30 = 180 < 195$) while the finalized match record stores raw reaction times of 210 ms for the Hunter and 195 ms for the Bounty

#### Scenario: Posse boost exceeding raw reaction time gives a negative effective draw time
- **WHEN** a challenger with 4 confirmed supporters ($60\text{ ms}$ boost) presses 45 ms after the draw window opens with 0 early presses, and the opponent presses at 10 ms with 0 boost
- **THEN** the challenger sends its raw reaction time of $45\text{ ms}$ and its boost of $60\text{ ms}$, both devices compute its effective draw time as $45 - 60 = -15\text{ ms}$, it wins the match ($-15 < 10$), and both result screens show $-15\text{ ms}$ for the challenger while the match record stores $45\text{ ms}$

#### Scenario: Opponent presses first and local player fails to press within 750 ms grace
- **WHEN** player $A$ receives a draw result of 220 ms from player $B$ and does not press within the 750 ms grace window
- **THEN** $A$ sends a never-pressed report to $B$, $A$ resolves the match as a loss, and $B$ resolves the match as a win

#### Scenario: Signed 0 ms result is accepted
- **WHEN** a standalone opponent's signed draw result carries a raw reaction time of $0\text{ ms}$
- **THEN** the receiver records it as the opponent's draw time
