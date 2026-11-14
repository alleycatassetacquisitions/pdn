## MODIFIED Requirements

### Requirement: Draw window, posse boost application, and result exchange

When the countdown completes, the device SHALL open the active draw window for up to **4000 ms** and record the local monotonic start timestamp:

- **First button down-edge press during the draw window (or post-receive grace window)**:
  - Both the primary and secondary buttons SHALL score on the button down-edge (`PRESS`, not button release/click resolution) so grip hold duration does not inflate reaction time; only the first button down-edge press in the draw window is counted and subsequent presses in the same match are ignored.
  - The device computes the player's raw reaction time (elapsed milliseconds since the draw window opened plus $75\text{ ms}$ per early countdown press) and effective draw time (raw reaction time minus posse supporter boost, with no floor).
  - The device records the raw reaction time in the player's personal reaction-time history and in its match draw slot (Hunter slot or Bounty slot) on the match record, snapshots the posse boost and early-press penalty, uses the effective draw time for winner determination and the result screen, and reliably sends a draw-result frame carrying its raw reaction time and its posse boost alongside its match draw slot to the opponent. The boost it applies and sends is at most a full posse's worth (18 supporters, $270\text{ ms}$).
- **Validating an incoming draw result from the opponent**:
  - The device SHALL accept a draw-result frame only if it comes from the active opponent for the active match ID, names the opponent's match draw slot (never the local player's draw slot), and carries a raw reaction time of $0\text{ ms}$ or more. No minimum raw reaction time applies: the countdown is a fixed rhythm, so an honest press that anticipates the draw can land at or near $0\text{ ms}$. A refused frame changes no state and nothing is sent in reply. It records the opponent's raw reaction time in the opponent's match draw slot on the match record, and takes the opponent's effective draw time for winner determination and the result screen as that raw time minus the frame's boost.
  - In a standalone match, a draw result or never-pressed report that passes these checks and the signed-frame check before the local draw window has opened SHALL be held, not refused, and applied the moment the window opens, exactly as one arriving at that instant would be. Only the latest held result counts. A held result SHALL NOT resolve the match before the local draw window opens, and it is discarded with the match key when the match ends or is abandoned (including a cable drop that abandons it). In a shootout match a result is applied when it arrives.
  - The device SHALL hold a received boost to a full posse's worth: a frame claiming more than $270\text{ ms}$ is treated as claiming $270\text{ ms}$.
- **Pressing before receiving the opponent's result (post-push wait)**:
  - If the local player presses before receiving a draw result or never-pressed report from the opponent, the device waits up to a **2500 ms** result grace period (`750 ms` opponent button-push grace window plus the `1500 ms` reliable-channel retry deadline plus a `250 ms` margin) for the opponent's result to arrive, then transitions to match resolution as soon as the opponent's result arrives or the 2500 ms grace period expires.
- **Receiving the opponent's result before pressing (post-receive wait)**:
  - If the device receives the opponent's draw result or never-pressed report while in the draw window before pressing locally, or the window opens on a held one, it records the opponent's draw time and enters a **750 ms** button-push grace window during which the local player can still press (scored on the button down-edge `PRESS`).
  - If the local player presses within that 750 ms grace window, the device sends its draw result and immediately resolves the match.
  - If the 750 ms grace window expires without a local press, the device records a pity draw time (elapsed time since draw window start), marks itself as never having pressed, reliably sends a never-pressed report to the opponent, and resolves the match as a loss.

#### Scenario: Posse boost reduces effective draw time for winner determination while preserving raw reaction times on the match record
- **WHEN** a Hunter challenger with 2 confirmed supporters ($30\text{ ms}$ boost) presses 210 ms after the draw window opens with 0 early presses, and the Bounty opponent presses at 195 ms with 0 boost
- **THEN** the Hunter records a personal and match-record raw reaction time of 210 ms, sends its 210 ms raw reaction time and 30 ms boost in its draw result, and wins the match ($210 - 30 = 180 < 195$) while the finalized match record stores raw reaction times of 210 ms for the Hunter and 195 ms for the Bounty

#### Scenario: Posse boost exceeding raw reaction time gives a negative effective draw time
- **WHEN** a challenger with 8 confirmed supporters ($120\text{ ms}$ boost) presses 90 ms after the draw window opens with 0 early presses, and the opponent presses at 100 ms with 0 boost
- **THEN** the challenger sends its raw reaction time of $90\text{ ms}$ and its boost of $120\text{ ms}$, both devices compute its effective draw time as $90 - 120 = -30\text{ ms}$, it wins the match ($-30 < 100$), and both result screens show $-30\text{ ms}$ for the challenger while the match record stores $90\text{ ms}$

#### Scenario: Opponent presses first and local player fails to press within 750 ms grace
- **WHEN** player $A$ receives a draw result of 220 ms from player $B$ and does not press within the 750 ms grace window
- **THEN** $A$ sends a never-pressed report to $B$, $A$ resolves the match as a loss, and $B$ resolves the match as a win

#### Scenario: Standalone draw result near 0 ms is accepted
- **WHEN** during a standalone match's open draw window a PDN receives, from the active opponent, a correctly signed draw result carrying a raw reaction time of $3\text{ ms}$
- **THEN** the PDN records $3\text{ ms}$ in the opponent's draw slot and takes it for winner determination

#### Scenario: Standalone result before the draw window opens is held until it opens
- **WHEN** a PDN still in the standalone match countdown receives, from the active opponent, a correctly signed draw result of $0\text{ ms}$, and its own draw window opens $30\text{ ms}$ later
- **THEN** the PDN records nothing and does not resolve the match during the countdown; when its window opens it records $0\text{ ms}$ in the opponent's draw slot and enters the 750 ms grace window, and both PDNs resolve the match the same way

#### Scenario: Held result and a boosted press within the grace window
- **WHEN** a Bounty holds a Hunter's draw result of raw $0\text{ ms}$ with a $100\text{ ms}$ boost, and presses $180\text{ ms}$ after its own window opens with a $270\text{ ms}$ boost
- **THEN** the Bounty records $180\text{ ms}$, compares $-100$ with $-90\text{ ms}$, and resolves the match as a loss

#### Scenario: Held result and no press within the grace window
- **WHEN** a PDN's window opens on a held draw result and the local player does not press within the 750 ms grace window
- **THEN** the PDN sends a never-pressed report with a pity draw time measured from its own window opening, and resolves the match as a loss

#### Scenario: Only the latest held result counts
- **WHEN** during the standalone countdown a PDN receives two correctly signed results from the active opponent, the second with the higher frame counter
- **THEN** when its draw window opens it applies only the second

#### Scenario: Held result is discarded with the match
- **WHEN** a PDN holds an early result and the match ends or is abandoned before the result is applied
- **THEN** the result is discarded with the match key and nothing of it applies to the next match

#### Scenario: Forged early result is not held
- **WHEN** during the standalone countdown a PDN receives a draw result from the opponent's MAC whose tag does not verify under the match key, or that carries no tag
- **THEN** it drops the frame, holds nothing, and its draw window opens with no opponent result

## ADDED Requirements

### Requirement: Same-player match refusal

A PDN whose own player UUID is non-nil SHALL NOT play a standalone match against an opponent whose player UUID equals its own, whether it initiated the match or was invited to it. An initiating PDN that receives a match acknowledgement naming its own non-nil player UUID SHALL clear the pending match and SHALL NOT enter the match countdown. A PDN SHALL NOT store a match record whose two player UUIDs are equal.

#### Scenario: Initiator refuses an acknowledgement that names its own player
- **WHEN** a Hunter registered as player $P$ has sent a match initiation and receives, from the Connected opponent, a match acknowledgement for that match carrying player UUID $P$
- **THEN** the Hunter clears the pending match, does not enter the match countdown, and stores no match record

#### Scenario: Invited PDN refuses an initiation that names its own player
- **WHEN** a Bounty registered as player $P$ receives a match initiation carrying player UUID $P$ from a Connected PDN on its opponent jack
- **THEN** the Bounty ignores the initiation and no match is primed

#### Scenario: No match record names the same player twice
- **WHEN** a match resolves on a PDN and the Hunter slot's player UUID equals the Bounty slot's player UUID
- **THEN** the PDN stores no match record for that match

#### Scenario: Demo players are not refused
- **WHEN** a demo Hunter with a nil player UUID receives a match acknowledgement carrying a nil player UUID
- **THEN** the same-player refusal does not apply, and the match proceeds as the other match requirements allow

### Requirement: Match key over the jack

At the start of each standalone match the Hunter SHALL draw a 16-byte match key from the hardware random number generator and send it, with the match ID, as a serial application frame of kind `0x03` on its opponent jack before the match initiation. The Bounty SHALL use a key only from the peer on its opponent jack once that peer is a Connected PDN. Both SHALL wipe the key when the match ends or is cleared.

#### Scenario: Hunter sends the match key before the initiation
- **WHEN** a Hunter starts a standalone match
- **THEN** it queues a kind `0x03` frame carrying the match ID and a fresh hardware-random key on its opponent jack, and only then sends the match initiation

#### Scenario: Jack refuses the match key
- **WHEN** the opponent jack refuses the Hunter's match key frame
- **THEN** the Hunter clears the pending match and sends no match initiation

#### Scenario: Key from another jack is ignored
- **WHEN** a Bounty receives a kind `0x03` frame on a jack other than its opponent jack
- **THEN** it keeps no key from it

#### Scenario: Key that arrives before the Bounty's link is Connected
- **WHEN** a Bounty whose opponent jack link is still connecting receives the Hunter's key frame and then its match initiation
- **THEN** it primes no match until that link is Connected to a PDN, and then primes the match from the held key and initiation without waiting for the Hunter to retry

#### Scenario: Key is wiped when the match ends
- **WHEN** a standalone match is finalized, abandoned or cleared
- **THEN** the PDN wipes the match key and its frame counters

### Requirement: Signed standalone match frames

Every standalone match frame except a role mismatch SHALL carry a frame counter and a 16-byte tag: the first 16 bytes of HMAC-SHA-256 under the match key. A PDN SHALL check tag, counter and sender role before duplicate detection, and SHALL drop a frame that fails, with no state change. A frame counter SHALL rise with each frame per direction; a counter at or below the last accepted one is refused.

#### Scenario: Forged result without the key is dropped
- **WHEN** during a keyed standalone match a PDN receives a draw result from the opponent's MAC whose tag does not verify under the match key
- **THEN** it drops the frame, records no result, and a later genuine result with the same sequence number is still accepted

#### Scenario: Replayed frame is refused
- **WHEN** a PDN receives a correctly tagged match frame whose counter is not above the last counter it accepted from that opponent in this match
- **THEN** it drops the frame

#### Scenario: Reflected frame is refused
- **WHEN** a PDN receives a correctly tagged match frame whose Hunter flag equals its own role
- **THEN** it drops the frame

#### Scenario: Initiation whose tag fails primes no match
- **WHEN** a Bounty holds a match key for match ID $M$ and receives a match initiation for $M$ whose tag does not verify under that key
- **THEN** it drops that initiation and no match is primed

#### Scenario: Shootout frames are not signed
- **WHEN** a PDN receives a shootout match frame
- **THEN** no match key, counter or tag check applies to it
