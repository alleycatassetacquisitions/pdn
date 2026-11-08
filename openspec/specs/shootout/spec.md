# shootout Specification

## Purpose

When PDN devices are cabled into a closed ring, they run a single-elimination tournament over ESP-NOW: one device coordinates, every member confirms, a bracket is drawn, and matches run one at a time until a single winner remains or the ring breaks. This spec covers the tournament protocol from ring closure to the end screens. Ring detection itself, chain roles, and the quickdraw mechanics inside a match belong to other capabilities.

Terms used throughout:

- **Ring**: a closed ring of serial-cabled devices, as reported by the ring detection layer. A device can be *on* a ring (membership, true on every device of the ring) or *head* it (the one device whose own identity came back around the ring; only the head is served a roster by ring detection).
- **Roster**: the set of devices in the ring, including the head.
- **Coordinator**: the device that drives the tournament.
- **Bracket**: the ordered participant list the coordinator draws.
- **Terminal phase**: ENDED or ABORTED.

## Requirements

### Requirement: Fault model

The protocol SHALL remain correct under the following conditions, and no requirement in this spec may assume otherwise:

- Any wireless frame can be lost or duplicated. Frames from one sender to one receiver arrive in the order they were sent, because each device has a single send queue with one frame in flight and drains a single receive queue. Frames from different senders interleave arbitrarily. A retransmission is a new frame, so it can arrive after frames sent later.
- A broadcast frame reaches every device in radio range, including devices in other rings.
- A transmit-success report from the radio confirms only that the frame was received over the air, not that the receiving application processed it.
- A cable can be pulled or re-seated at any moment. The ring layer reports a link lost only after it has been silent for about 100 ms, so ring membership can lag the physical cabling.
- Frames are delivered and processed within milliseconds, and every protocol timer is 100 ms or longer, so a timer does not fire on a frame that is merely late. A main loop stalled past a timer breaks this assumption.
- A device can reset or lose power at any moment. While it reboots, frames sent to it are lost, and it returns with no tournament state.
- Two rings can close independently and then be cabled into one, and a single ring can close at more than one point, so more than one device can believe it heads the ring.

#### Scenario: Foreign-ring frames are ignored
- **WHEN** a device receives a tournament command whose sender or roster does not include this device's ring
- **THEN** the device applies nothing and sends no acknowledgement

#### Scenario: Radio success is not delivery
- **WHEN** a reliable command's transmit is reported successful but no acknowledgement arrives from a recipient
- **THEN** the sender treats that recipient as not yet reached and keeps retransmitting

### Requirement: Command delivery

All tournament commands SHALL share one packet type, distinguished by a leading command byte, and SHALL be sent as a single broadcast frame per fan-out rather than one unicast per member. The per-device ESP-NOW peer table holds only 20 entries, so a unicast fan-out could not reach a larger ring.

The reliable commands are BRACKET, MATCH_START, MATCH_RESULT, TOURNAMENT_END, and ABORT. Each carries a non-zero sequence ID drawn from a single per-device counter that skips 0. Each recipient acknowledges with an ACK frame naming that sequence ID, sent to the sender. The sender tracks acknowledgements per recipient and retransmits the frame to the recipients still outstanding, with exponential backoff starting at 100 ms for up to three retransmissions. Once the retransmissions run out, the sender abandons the remaining recipients and applies the abandonment policy.

RING_CLOSED and CONFIRM carry sequence ID 0, are never acknowledged, and are rebroadcast once per second while needed.

#### Scenario: Acknowledgement stops retransmission for one recipient
- **WHEN** a reliable fan-out is outstanding to members A and B and A acknowledges its sequence ID
- **THEN** further retransmissions of that frame are owed only to B

#### Scenario: Retry budget exhausted
- **WHEN** a recipient has not acknowledged a reliable frame after the initial send and three retransmissions
- **THEN** the sender abandons that recipient and applies the abandonment policy for that command

### Requirement: Coordinator claim on ring closure

When ring detection reports that this device closed a ring and no tournament is running, the device SHALL claim coordinator, record the roster it is served (plus itself), and broadcast RING_CLOSED carrying that roster. The claim SHALL NOT start a tournament by itself. Entry into the proposal phase is decided by the proposal entry predicate.

#### Scenario: Head closes a ring while idle
- **WHEN** a device in phase IDLE is told it closed a ring with roster {A, B, C} (itself A)
- **THEN** it records itself as coordinator, records {A, B, C} as the ring roster, and broadcasts RING_CLOSED with that roster

#### Scenario: Ring closes during a tournament
- **WHEN** a device not in phase IDLE is told it closed a ring
- **THEN** it ignores the event

### Requirement: Adopting a ring-closed announcement

A device in phase IDLE that receives RING_CLOSED SHALL adopt the sender as coordinator and the carried roster as its ring roster, provided the roster includes this device. If this device holds its own claim and still heads the ring, it SHALL adopt only an announcement from a lower MAC address. Otherwise two claimants that each adopted the other would leave the ring with no coordinator.

#### Scenario: Member adopts the coordinator
- **WHEN** an idle device with no claim receives RING_CLOSED from X whose roster includes it
- **THEN** it records X as coordinator and the carried roster as its ring roster

#### Scenario: Announcement for another ring
- **WHEN** an idle device receives RING_CLOSED whose roster does not include it
- **THEN** it ignores the announcement

#### Scenario: Competing claimant with a higher MAC
- **WHEN** a device that claimed and still heads the ring receives RING_CLOSED from a higher MAC address
- **THEN** it keeps its own claim

#### Scenario: Competing claimant with a lower MAC
- **WHEN** a device that claimed and still heads the ring receives RING_CLOSED from a lower MAC address
- **THEN** it adopts the sender as coordinator and the sender's roster

### Requirement: Proposal entry

A device SHALL enter the proposal phase when it is in phase IDLE, its game is in the idle state, and one of these holds:

- It holds a coordinator claim for itself and still heads the ring.
- It has adopted another device's announcement and is still on a ring.
- It holds no claim and heads a ring. This covers a ring that stayed cabled through a reset, whose closure event has already fired.

Proposal entry SHALL take priority over starting an ordinary match from the idle state. On entry the device SHALL clear all state from any previous tournament except its coordinator claim and ring roster. If it has no roster but heads the ring, it SHALL claim coordinator again and broadcast RING_CLOSED.

#### Scenario: Deposed head does not propose
- **WHEN** an idle device holds a self-claim but no longer heads the ring
- **THEN** it does not enter proposal

#### Scenario: Member left the ring before proposing
- **WHEN** an idle device has adopted an announcement but is no longer on a ring
- **THEN** it does not enter proposal

#### Scenario: Re-forming after an abort with cables unchanged
- **WHEN** a device that heads a still-closed ring returns to idle with no claim and no roster
- **THEN** it enters proposal, claims coordinator, and broadcasts RING_CLOSED with its current roster

#### Scenario: Ring closes while two members could play a match
- **WHEN** an idle device satisfies both the proposal entry predicate and the conditions for starting an ordinary match
- **THEN** it enters proposal

### Requirement: Confirmation

During the proposal phase, a button press SHALL add the local device to its confirmed set and broadcast CONFIRM carrying its MAC and its player name (truncated or null-padded to 12 bytes). A device that has confirmed SHALL rebroadcast CONFIRM once per second until every roster member has confirmed. A button press outside the proposal phase SHALL have no effect.

A device in the proposal phase that receives CONFIRM SHALL record the originator and its name, but only if the originator is already confirmed or is in the roster this device holds. The coordinator SHALL rebroadcast RING_CLOSED once per second, re-reading its roster each time, until every roster member has confirmed. This covers a member that missed the first announcement or joined the roster late.

#### Scenario: Local confirm
- **WHEN** the player presses a button during proposal
- **THEN** the device records itself as confirmed and broadcasts CONFIRM with its name

#### Scenario: Stray confirm from outside the ring
- **WHEN** a device in proposal receives a first CONFIRM from a MAC outside its roster
- **THEN** it does not record it

#### Scenario: Press after the proposal closed
- **WHEN** a stale button callback fires in phase BRACKET_REVEAL
- **THEN** nothing is recorded or sent

### Requirement: Bracket draw

When every member of a roster of at least two devices has confirmed, a device SHALL advance to phase BRACKET_REVEAL. If that device is the coordinator, it SHALL draw the bracket as a random permutation of its confirmed set and broadcast it reliably to the bracket. The BRACKET frame SHALL carry at most 64 entries. A member past that limit is missing from the frame it receives, so it never acknowledges, and its abandonment aborts the tournament. Other devices SHALL wait for the coordinator's BRACKET.

#### Scenario: Coordinator draws the bracket
- **WHEN** the coordinator's roster is {A, B, C, D} and all four have confirmed
- **THEN** it enters BRACKET_REVEAL, draws a permutation of {A, B, C, D}, and sends BRACKET reliably to B, C, and D

#### Scenario: Ring larger than the bracket frame
- **WHEN** 65 members confirm
- **THEN** the member left out of the BRACKET frame never acknowledges, and the coordinator aborts once it is abandoned

#### Scenario: Roster below the floor
- **WHEN** a roster holds fewer than two devices
- **THEN** the proposal does not advance, however many devices have confirmed

### Requirement: Short-roster abort

A coordinator in the proposal phase that has confirmed locally and whose roster stays below two devices for 3 seconds SHALL abort the tournament. A coordinator that has not confirmed SHALL stay in the proposal phase instead.

#### Scenario: Lone device confirms
- **WHEN** a coordinator whose roster is only itself confirms and the roster stays that way for 3 seconds
- **THEN** it aborts and shows the ABORTED screen

#### Scenario: Untouched self-cabled device
- **WHEN** a coordinator whose roster is only itself never confirms
- **THEN** it stays in the proposal phase

### Requirement: Adopting a bracket

A device receiving BRACKET SHALL ignore it unless the bracket includes this device, and SHALL ignore it in a terminal phase. A coordinator receiving a bracket from a higher MAC address SHALL ignore it. A coordinator receiving a bracket from a lower MAC address SHALL give up its claim, discard its own bracket, match progress, and in-flight commands, and adopt the received bracket. Any adopting device SHALL record the sender as coordinator, enter BRACKET_REVEAL, and acknowledge. A duplicate BRACKET SHALL be acknowledged again without being re-applied.

#### Scenario: Member adopts the bracket
- **WHEN** a non-coordinator in proposal receives BRACKET from X that includes it
- **THEN** it adopts the bracket, records X as coordinator, enters BRACKET_REVEAL, and acknowledges

#### Scenario: Merged rings, lower MAC wins
- **WHEN** coordinator B, holding its own bracket, receives BRACKET from coordinator A with a lower MAC that includes B
- **THEN** B discards its bracket and progress, adopts A's bracket, records A as coordinator, and acknowledges

#### Scenario: Merged rings, higher MAC loses
- **WHEN** coordinator A receives BRACKET from coordinator B with a higher MAC
- **THEN** A ignores it and keeps its own bracket

#### Scenario: Late bracket after the tournament ended
- **WHEN** a device in ENDED or ABORTED receives a retransmitted BRACKET that includes it
- **THEN** it ignores the frame

### Requirement: Match scheduling

The coordinator SHALL start the first match only after every bracket recipient has acknowledged or been abandoned and the 5-second bracket reveal has elapsed. Matches pair the current round in order: entries 0 and 1, then 2 and 3, and so on. With an odd count, the last entry has a bye and carries into the next round. When a round is exhausted:

- One survivor: the coordinator ends the tournament with that survivor as winner.
- Two or more: the survivors become the next round.
- No survivors: the coordinator aborts.

To start a match, the coordinator SHALL enter MATCH_IN_PROGRESS and send MATCH_START, carrying both paired players and the match index, reliably to the bracket. If the coordinator is one of the paired players, it primes its own match locally, since it does not receive its own broadcast.

#### Scenario: Four-player tournament
- **WHEN** the bracket is [A, B, C, D] and A beats B, then C beats D
- **THEN** the next round is [A, C], and the winner of A vs C is declared tournament winner

#### Scenario: Odd bracket
- **WHEN** the bracket is [A, B, C] and A beats B
- **THEN** the next round is [A, C]

#### Scenario: Both paired players reported a win
- **WHEN** a round ends with every entry eliminated
- **THEN** the coordinator aborts the tournament

### Requirement: Match participation

A device receiving MATCH_START SHALL apply it only if the sender is its coordinator, the device is not the coordinator, and it is not in a terminal phase. It SHALL acknowledge any MATCH_START that passes those checks, then ignore the frame if a paired player is outside its bracket, if the frame is a duplicate, if either paired player is already eliminated, or if it names the match already in progress. Otherwise it SHALL record the pairing and enter MATCH_IN_PROGRESS.

Within a match, the paired player with the lower MAC address SHALL be the hunter and the other the bounty. This role belongs to the match only and SHALL NOT change the player's standing hunter/bounty role. Both paired players SHALL derive the same match ID from the match index ("SHT-" followed by the index zero-padded to 32 digits), so no ID exchange is needed. A shootout match SHALL NOT write lifetime player statistics.

#### Scenario: Spectator
- **WHEN** a member not named in MATCH_START applies it
- **THEN** it enters MATCH_IN_PROGRESS and shows the two paired players' names

#### Scenario: Late announcement of a finished match
- **WHEN** a member receives MATCH_START for a pairing where one paired player is already eliminated
- **THEN** it acknowledges and changes nothing

#### Scenario: Per-match role
- **WHEN** paired players with MACs 0x10… and 0x20… are matched
- **THEN** 0x10… plays hunter and 0x20… plays bounty for that match, and neither player's standing role changes

### Requirement: Match resolution

The paired player who wins a match SHALL report it exactly once by sending MATCH_RESULT (winner, loser, match index) reliably to the bracket, and SHALL apply the result locally. If the match timer expires without a press, the match's hunter forfeits and its bounty reports the win.

A device receiving MATCH_RESULT SHALL ignore it unless the sender is in its bracket. It SHALL acknowledge it, and apply it only if the winner and loser are both in its bracket and the loser is not already eliminated. Applying a result eliminates the loser and moves the device to BETWEEN_MATCHES, but only if the result names the device's current match. A result for an earlier match records the elimination without pulling the device out of a newer match. A device in a terminal phase records the elimination and stays in that phase.

#### Scenario: Match timeout
- **WHEN** the match timer expires in a shootout match with neither paired player pressing
- **THEN** the bounty reports the win and the hunter is eliminated

#### Scenario: Duplicate result
- **WHEN** a device receives MATCH_RESULT whose loser is already eliminated
- **THEN** it acknowledges and changes nothing

#### Scenario: Late result for an earlier match
- **WHEN** a device in match 3 receives a first-time MATCH_RESULT for match 2
- **THEN** it eliminates match 2's loser and stays in match 3

### Requirement: Abandonment policy

An abandoned recipient SHALL end the tournament only when its silence blocks the tournament, and the question SHALL be asked about the abandoned frame's own contents, not current state:

- BRACKET abandoned to a bracket member who is not eliminated: abort.
- MATCH_START abandoned to one of the paired players that frame names: abort.
- MATCH_RESULT abandoned to the coordinator: re-send that result once per match, because the coordinator advances only on receiving a result.
- Any other abandonment: log it and continue.

#### Scenario: Spectator misses a match start
- **WHEN** MATCH_START is abandoned to a member who is not a paired player in that frame
- **THEN** the tournament continues

#### Scenario: Paired player unreachable
- **WHEN** MATCH_START is abandoned to a paired player named in that frame
- **THEN** the coordinator aborts the tournament

#### Scenario: Coordinator missed a result
- **WHEN** a winner's MATCH_RESULT for match 2 is abandoned to the coordinator for the first time
- **THEN** the winner re-sends that result, and does not re-send it again if the retry is also abandoned

### Requirement: Tournament end

On declaring a winner, the coordinator SHALL enter ENDED and send TOURNAMENT_END carrying the winner reliably to its confirmed set, which includes eliminated players. A device receiving TOURNAMENT_END SHALL apply it only if the sender is its coordinator. It SHALL acknowledge it, and enter ENDED with that winner if the winner is in its bracket. The ENDED screen SHALL remain until the device is no longer on a ring. The device then passes through sleep and clears all tournament state.

#### Scenario: Eliminated player sees the result
- **WHEN** the coordinator declares winner C and member D was eliminated in round one
- **THEN** D receives TOURNAMENT_END and shows the final standings with C as winner

#### Scenario: Standings persist while cabled
- **WHEN** a device is in ENDED and the ring stays closed
- **THEN** it keeps showing the final standings

### Requirement: Abort

A device SHALL abort the tournament when any of these happens:

- It has been off the ring for 500 ms continuously during a non-terminal active phase.
- The abandonment policy calls for an abort.
- The short-roster timeout fires.
- A round ends with no survivors.

Aborting SHALL do nothing in a terminal phase. Otherwise the device SHALL clear all tournament state, including its coordinator claim and roster, enter ABORTED, and send ABORT reliably to its bracket, or to its confirmed set if no bracket exists. The ABORT fan-out and any TOURNAMENT_END in flight SHALL keep retransmitting through later resets. Every other in-flight command SHALL be cancelled.

A device receiving ABORT SHALL ignore it unless the sender is in its bracket, confirmed set, or roster. It SHALL acknowledge it, and SHALL abort locally without re-broadcasting, unless it is in IDLE or a terminal phase. The ABORTED screen SHALL show for 2 seconds, and then the device returns to idle.

#### Scenario: Cable flap absorbed
- **WHEN** a device in the proposal phase leaves the ring and is back on it within 500 ms
- **THEN** the tournament continues

#### Scenario: Sustained ring break
- **WHEN** a device in MATCH_IN_PROGRESS stays off the ring for 500 ms
- **THEN** it aborts and sends ABORT to its bracket

#### Scenario: Stray abort after a win
- **WHEN** a device in ENDED receives ABORT from a ring member
- **THEN** it acknowledges and keeps showing the final standings

#### Scenario: Abort from another ring
- **WHEN** a device receives ABORT from a MAC outside its bracket, confirmed set, and roster
- **THEN** it neither acknowledges nor applies it

### Requirement: Terminal phases are final

Once a device is in ENDED or ABORTED, no inbound command SHALL move it to a non-terminal phase. Only the terminal screen's own exit, which clears all tournament state, may do so.

#### Scenario: Retransmitted match start after the end
- **WHEN** a device in ENDED receives MATCH_START from its coordinator
- **THEN** it does not re-enter MATCH_IN_PROGRESS
