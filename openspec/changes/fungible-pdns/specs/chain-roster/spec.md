## MODIFIED Requirements

### Requirement: Posse roles, challenger cascade, and supporter boost

Outside of a closed ring, PDNs cabled together with the same standing role (Hunter or Bounty) form a **posse**:

- **Jack orientation by role**:
  - For a **Hunter**, the **opponent jack** is the **downstream jack** and the **supporter jack** is the **upstream jack** (Hunter supporters chain upstream behind their challenger).
  - For a **Bounty**, the **opponent jack** is the **upstream jack** and the **supporter jack** is the **downstream jack** (Bounty supporters chain downstream behind their challenger).
- **Challenger vs supporter role**:
  - A PDN is a **supporter** if and only if it is not on a ring and the Connected peer on its **opponent jack** is a PDN with the same standing role as this device.
  - A PDN is a **challenger** if and only if it is not on a ring and it is not a supporter.
  - When a device is on a ring, it is neither a challenger nor a supporter.
- **Challenger identity cascade and posse enrollment**:
  - A challenger adopts its own MAC as its challenger identity and reliably announces that challenger MAC to the Connected same-role peer on its **supporter jack** (re-sending periodically as a backstop).
  - A supporter that receives a non-zero challenger announcement from the same-role peer on its **opponent jack** adopts that challenger MAC, forwards that challenger MAC to the Connected peer on its own **supporter jack**, and sends a reliable posse-join message directly to the challenger.
  - A challenger's posse member list consists of its direct same-role `PDN` neighbor on its **supporter jack** (established by the physical cable alone, provided the neighbor's device type is `PDN`; an `FDN` cabled on the supporter jack SHALL never be counted as a posse supporter or grant a boost) plus any indirect `PDN` supporters that have sent a posse-join naming this challenger (up to 20 supporters; when all 20 indirect supporter slots are occupied, additional posse-join requests from new MAC addresses SHALL be ignored rather than evicting existing supporters).
  - While a challenger's **supporter jack** has no Connected same-role `PDN` neighbor, the challenger SHALL ignore wireless posse-join and posse-confirmation frames. When a challenger's **supporter jack** loses its same-role `PDN` neighbor, the challenger SHALL immediately clear both its posse member list and all recorded supporter confirmations.
- **Posse round lifecycle and draw-time boost**:
  - **Countdown**: When a challenger starts a match countdown, it broadcasts an unacknowledged countdown posse event naming itself as challenger and clears all supporter confirmations from any previous round. A supporter receiving a countdown event from its own challenger clears any prior confirmation and arms its button for posse confirmation.
  - **Supporter confirmation and $+15\text{ ms}$ boost**: When an armed supporter presses a button before the draw window opens, it locks in its confirmation and sends a reliable confirmation message to its challenger (re-sending if its challenger identity updates while the confirmation is standing; leaving the supporter role clears the standing confirmation). The confirmation message SHALL carry the supporter's own 16-byte player UUID, which is the nil UUID for a PDN that is not registered and for a PDN on the demo profile. Each eligible confirmed supporter currently in the challenger's posse member list grants the challenger a **15 ms draw-time boost** (reducing the challenger's effective draw time in the match).
  - **Supporter eligibility**: A confirmed supporter SHALL be eligible only if the player UUID in its confirmation is non-nil and differs from the challenger's own player UUID. A supporter that is not registered, a supporter on the demo profile, and a supporter registered to the challenger's own player SHALL grant no boost. A challenger on the demo profile, whose own player UUID is nil, SHALL be boosted by every confirmed supporter whose player UUID is non-nil. Supporters SHALL NOT be compared with each other. The challenger SHALL log each confirmation that fails this test, every time it receives one. Eligibility SHALL change nothing else: the posse member list, the cascade, the events a supporter receives and what a supporter displays are the same for an eligible supporter and one that is not.
  - **Draw window open**: When the challenger enters the active draw phase of the match, it broadcasts an unacknowledged draw posse event naming itself; supporters of that challenger disarm their buttons so presses after the draw window opens are ignored.
  - **Match result**: When the challenger finishes the match, it reliably broadcasts a posse win or loss event (superseding any earlier terminal posse event) to its posse member list. Supporters of that challenger acknowledge the event, display the posse win/loss outcome (distinguishing whether the supporter confirmed or missed the countdown) for 8 seconds, and then reset to the posse-ready state.

#### Scenario: Multi-hop Hunter posse enrollment and boost calculation
- **WHEN** Hunters $S_2 \to S_1 \to C_H$ are cabled (`S2.downstream -> S1.upstream`, `S1.downstream -> CH.upstream`), $C_H$'s downstream jack connects to Bounty challenger $C_B$, and $S_1$ and $S_2$ are each registered to a player other than $C_H$'s
- **THEN** $C_H$ is the Hunter challenger, $S_1$ and $S_2$ become supporters following $C_H$, and when both $S_1$ and $S_2$ press during the countdown, $C_H$ receives a $30\text{ ms}$ ($2 \times 15\text{ ms}$) draw-time boost

#### Scenario: Multi-hop Bounty posse enrollment in reverse cable direction
- **WHEN** Hunter challenger $C_H$'s downstream jack connects to Bounty $C_B$'s upstream jack, and Bounties $C_B \to B_1 \to B_2$ are cabled downstream (`CB.downstream -> B1.upstream`, `B1.downstream -> B2.upstream`)
- **THEN** $C_B$ is the Bounty challenger and $B_1$ and $B_2$ become supporters following $C_B$

#### Scenario: Unplugging the challenger's supporter jack clears posse and boost
- **WHEN** supporter $S_1$ unplugs from challenger $C_H$'s supporter jack after $S_1$ and $S_2$ had confirmed
- **THEN** $C_H$ clears its posse roster and confirmations, and its draw-time boost drops to $0\text{ ms}$

#### Scenario: New countdown voids confirmations from a prior round
- **WHEN** a supporter confirmed in round 1 and its challenger broadcasts a countdown posse event for round 2
- **THEN** both the challenger and the supporter clear the round 1 confirmation and the supporter must press again during round 2's countdown to grant a boost

#### Scenario: Supporter registered as the challenger's own player grants no boost
- **WHEN** challenger $C_H$ is registered to player $P$, its supporters $S_1$ and $S_2$ both confirm during the countdown, $S_1$ is registered to player $P$ and $S_2$ to a different player
- **THEN** $C_H$ receives a $15\text{ ms}$ draw-time boost, for $S_2$ alone

#### Scenario: Supporter that is not registered to a player grants no boost
- **WHEN** a supporter on the demo profile, whose player UUID is nil, confirms during its challenger's countdown
- **THEN** the challenger receives no draw-time boost for that supporter

#### Scenario: Demo challenger is boosted by a registered supporter
- **WHEN** a challenger on the demo profile has one supporter registered to a player and that supporter confirms during the countdown
- **THEN** the challenger receives a $15\text{ ms}$ draw-time boost

#### Scenario: Supporter that does not count is shown the same outcome
- **WHEN** a supporter registered to its challenger's own player confirms during the countdown and the challenger then wins
- **THEN** the supporter stays in the challenger's posse member list, receives the posse win event, and displays the same outcome as a supporter that counted
