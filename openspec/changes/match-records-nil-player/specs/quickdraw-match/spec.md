## MODIFIED Requirements

### Requirement: Match initiation and identity

At most one match SHALL be active on a device at any time. Every match SHALL have a match ID shared by both paired players (a 16-byte UUID for standalone matches, or the deterministic tournament match identity for shootout matches) and a local match draw slot (Hunter slot or Bounty slot) fixed when the match is primed:

- **Standalone match initiation**:
  - Only a Hunter challenger (a provisioned player with a non-nil player UUID, or a demo player with a nil player UUID) that is idle, not on a ring, and has a Connected Bounty PDN on its downstream (opponent) jack SHALL initiate a standalone match, minting a fresh 16-byte UUID match ID and sending a match-initiation frame (carrying the match UUID, the Hunter's 16-byte player UUID, 16-byte team UUID, 6-byte MAC address, and the Hunter role) to that Bounty peer.
  - A device receiving a match-initiation frame SHALL ignore it unless no match is already active, the sender's player UUID is not the receiver's own non-nil player UUID, and the sender is a currently Connected PDN on the receiver's opponent jack. A nil player UUID on either side marks a demo player: the match runs, but is not stored for collection (`match-collection`).
  - If the receiver is not in a shootout and has the same standing role as the sender (an attempted same-role showdown), the receiver SHALL reject the initiation by replying with a role-mismatch frame carrying that match UUID; the initiator receiving a role-mismatch reply for its active match SHALL clear the pending match.
  - Otherwise, if the receiver is an idle Bounty challenger (not on a ring and not a posse supporter), the receiver SHALL prime its active match with the received match UUID, record the opponent's player UUID and team UUID in the opponent's draw slot, and send a reliable match-acknowledgement frame carrying the match UUID and its own 16-byte player UUID, 16-byte team UUID, and 6-byte MAC address.
  - To synchronize countdown entry within a single wireless frame propagation delay (~1–2 ms) even when retransmissions occur, the responding Bounty SHALL transition into the match countdown upon confirmed link-layer delivery of its match-acknowledgement frame (and SHALL clear the primed match if delivery of the match acknowledgement is abandoned), while the initiating Hunter SHALL record the Bounty's player UUID and team UUID, stop retransmitting the match initiation, and transition into the match countdown immediately upon receiving the matching match acknowledgement.
  - If the initiator does not receive the match acknowledgement before the `1000 ms` match initialization timeout expires, it SHALL clear the pending match so a fresh initiation can occur if the cable remains connected.
- **Shootout match priming**:
  - When a shootout match-start command pairs two players, each paired player primes its active match locally using the deterministic match ID derived from the tournament match identity (`shootout`) and assigns the Hunter draw slot to the lower MAC address and the Bounty draw slot to the higher MAC address, without exchanging a standalone match handshake and without mutating either player's standing role.

#### Scenario: Standalone handshake exchanges full identity and synchronizes countdown start
- **WHEN** Hunter $H$ sends a match initiation with match UUID $M$ to Connected Bounty $B$ on its opponent jack and $B$'s match acknowledgement for UUID $M$ is delivered to $H$
- **THEN** both $H$ and $B$ record match $M$ with each other's 16-byte player UUIDs and 16-byte team UUIDs, and enter the match countdown simultaneously upon delivery of the match acknowledgement

#### Scenario: Same-role showdown initiation is rejected with role mismatch
- **WHEN** a device receives a match initiation from a direct neighbor whose role equals the receiver's own standing role outside of a shootout
- **THEN** the receiver does not prime a match and replies with a role-mismatch rejection, causing the initiator to clear its pending match

#### Scenario: Shootout match preserves standing role
- **WHEN** two standing Bounties $B_1$ and $B_2$ ($\text{MAC}(B_1) < \text{MAC}(B_2)$) are paired in a shootout match
- **THEN** $B_1$ is assigned the Hunter draw slot and $B_2$ the Bounty draw slot for that match, and both players' standing roles remain Bounty during and after the match

#### Scenario: Match with a demo player is played but not stored
- **WHEN** a demo Hunter with a nil player UUID initiates a match with a provisioned Bounty and the match resolves
- **THEN** both devices play the match and show the result, and neither stores a match record

#### Scenario: Tied match is played but not stored
- **WHEN** two provisioned players finish a match with equal effective draw times
- **THEN** both devices show a loss, and neither stores a match record, because no winner exists for both records to name

#### Scenario: Match whose opponent result never arrived is not stored
- **WHEN** a provisioned player's result grace period expires without the opponent's draw result
- **THEN** that device shows a loss and stores no match record, leaving the record to the opponent's device if it received both results

#### Scenario: Recorded winner follows the never-pressed rule before the draw times
- **WHEN** one provisioned player presses and the other never does, whatever the two effective draw times
- **THEN** both devices store a record naming the player who pressed as the winner; if neither pressed, neither device stores a record
