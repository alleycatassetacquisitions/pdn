# chain-roster Specification

## Purpose

When PDN and FDN devices are cabled together through their directional serial jacks, the cabled topology layer discovers direct neighbors, verifies two-way wireless reachability, exchanges peer profiles, tracks multi-hop chains and closed rings, maintains the head's chain roster, arbitrates which cabled game mode applies (Shootout, 1v1 Quickdraw match, Posse, or FDN Symbol match), and coordinates same-role Posse supporter chains during a match.

This specification states the required outcomes, invariants, and environmental constraints of all cabled mechanics, independent of internal classes, data structures, or wire encodings.

Terms used throughout:

- **Upstream jack**: a device's incoming serial jack, which connects to at most one upstream neighbor at a time. (An FDN also has a **secondary upstream jack** used exclusively for FDN symbol match.)
- **Downstream jack**: a device's outgoing serial jack, which connects to at most one downstream neighbor at a time.
- **Opponent jack**: the jack on which a PDN faces an opposing player — the **downstream jack** for a Hunter, and the **upstream jack** for a Bounty.
- **Supporter jack**: the jack on which a PDN chains same-role posse supporters — the **upstream jack** for a Hunter, and the **downstream jack** for a Bounty.
- **Open chain**: a directed path of serial-cabled devices $H \to n_1 \to \dots \to n_k$ where $H$'s upstream jack is disconnected and $n_k$'s downstream jack is disconnected.
- **Ring**: a closed ring of serial-cabled devices in which the tail's downstream jack is connected to the head's upstream jack.
- **Head**: the root device of an open chain (downstream jack connected, upstream jack disconnected) or the device that closed a ring (its own identity returned around the ring to its upstream jack).
- **Roster**: the directed tree of downstream members held by a head, mapping each known member to its immediate upstream neighbor (`member -> upstream`).
- **Posse**: a same-role chain of PDNs rooted at a **challenger** and extending through one or more **supporters** along their supporter jacks.
- **Challenger**: the root PDN of a posse that faces the opponent and fights the match.
- **Supporter**: a PDN cabled to a same-role PDN on its opponent jack that backs its posse's challenger during a match to grant a draw-time boost.

## Requirements

### Requirement: Physical and network fault model

The capability SHALL remain correct under the following conditions, and no requirement in this spec may assume otherwise:

- Each jack connects to at most one serial neighbor at any instant.
- Serial beacons travel one hop from a sender's jack to the neighbor's cabled jack every 20 ms. Consequently, topology and head identity propagate downstream one hop at a time: in a chain $H \to \dots \to u \to d$, $d$ learns of $H$ only after $u$ has learned of $H$.
- During physical plug insertion or removal, TRS jack contacts can transiently short a device's serial transmit pin back to its own receive pin (self-echo).
- Link detection and link-loss detection lag physical cabling (~100 ms silence window), and a serial link can fail asymmetrically (one side times out its jack and re-handshakes while the other side never observed a disconnect).
- A serial beacon proves only one-way physical contact; it does not by itself prove that the neighbor can receive or reply over the wireless channel.
- Any wireless frame can be lost or duplicated. Frames from different senders interleave arbitrarily, and a wireless frame triggered by a jack event can arrive at the peer before the peer has parsed the corresponding serial beacon.
- Retransmissions can arrive after later topology changes have already occurred.
- A transmit-success report or acknowledgement from a former head does not mean a newly merged head has received the update.
- Cables can be connected, disconnected, moved to a different peer, or plugged back into the same peer at any moment. Multiple chains or rings can form independently and merge into one, or split apart.

#### Scenario: Self-echo on jack insertion is ignored
- **WHEN** a jack receives a serial beacon carrying an all-zero identity or the device's own identity
- **THEN** the device ignores the frame without opening a link or refreshing link liveness

#### Scenario: Out-of-order wireless arrivals across senders
- **WHEN** two devices in a chain send roster updates to the head and the network delivers them in either order
- **THEN** the head's roster converges to the same physical topology once both updates are delivered

#### Scenario: Duplicate wireless frame on lost acknowledgement
- **WHEN** a reliable update is processed by a recipient, its acknowledgement is lost over the air, and the sender retransmits the frame
- **THEN** the recipient acknowledges the retransmission without re-applying the update

### Requirement: Single-hop jack link lifecycle and context exchange

Each serial jack SHALL progress through three states — **Disconnected**, **Connecting**, and **Connected**:

- **Disconnected to Connecting**: When a Disconnected jack receives a valid serial beacon from a neighbor, it records that neighbor's identity, enters **Connecting**, and sends this device's context (device type `PDN` or `FDN`, current chain role, and player profile including standing Hunter/Bounty role and player ID) reliably over the wireless channel to that neighbor.
- **Connecting to Connected**: A Connecting jack SHALL transition to **Connected** only after a wireless context from that same neighbor has been received and recorded on the jack.
- **Early wireless context buffering**: If a neighbor's wireless context arrives before the local jack has entered **Connecting** for that neighbor (or before the second jack of a two-device ring has entered **Connecting**), the device SHALL buffer that context for up to the link-silence window (100 ms) and apply it as soon as the matching jack enters **Connecting**.
- **Link loss and half-open timeout**: If a Connecting or Connected jack hears no valid serial beacon for 100 ms, or if a Connecting jack does not receive the neighbor's wireless context within 500 ms, the jack SHALL return to **Disconnected** and clear all stored identity, device type, and profile state for the departed neighbor.
- **Immediate peer swap**: If a Connecting or Connected jack receives a valid serial beacon whose identity differs from the neighbor currently tracked on that jack, the jack SHALL immediately tear down the prior link through **Disconnected** before entering **Connecting** for the new neighbor.
- **Live profile update**: When a device's exported profile (such as its standing Hunter/Bounty role) changes while one or more jacks are **Connected**, the device SHALL push a fresh wireless context to each connected neighbor, and a neighbor receiving a fresh context on an already **Connected** jack SHALL update its stored peer profile and notify its local listeners.

#### Scenario: Half-open link does not report Connected
- **WHEN** a jack receives serial beacons from neighbor $B$ but $B$'s wireless context never arrives
- **THEN** the jack remains in **Connecting** (never **Connected**) and resets to **Disconnected** after 500 ms

#### Scenario: Wireless context arriving before serial beacon completes connection
- **WHEN** neighbor $B$'s wireless context arrives at $A$ 20 ms before $A$ parses $B$'s first serial beacon on a jack
- **THEN** $A$ buffers the context and transitions the jack to **Connected** as soon as the serial beacon arms **Connecting**

#### Scenario: Fast cable swap to a new peer before silence timeout
- **WHEN** a Connected jack tracking neighbor $B$ receives a serial beacon from neighbor $C$ ($C \ne B$) within 50 ms
- **THEN** the jack tears down $B$'s link and state first and then enters **Connecting** for $C$

### Requirement: Head identity, ring closure, and ring membership relay

Only the primary **upstream jack** and **downstream jack** of `PDN` devices participate in chain head propagation and ring detection:

- An `FDN` is a valid direct jack peer (for player provisioning, symbol match, or single-player training), but an `FDN` SHALL NOT act as a chain link, SHALL NOT adopt or propagate a chain head, SHALL NOT latch or relay ring closure, and SHALL never report a chain role of `RING` or claim shootout coordinator.
- A jack on a `PDN` whose Connected neighbor has device type `FDN` SHALL NOT count as a chain link for chain-role derivation or ring closure; a `PDN` whose only Connected jack is to an `FDN` SHALL report chain role `STANDALONE` rather than `HEAD` or `CHILD`.
- The `FDN` secondary upstream jack SHALL NOT adopt a head, propagate a head, or open/close a ring.

A `PDN` with a Connected downstream jack (to another `PDN`) and no `PDN`-connected upstream jack SHALL act as the head of its chain and advertise its own identity downstream.

A `PDN` with a Connected upstream jack (to another `PDN`) SHALL adopt the head identity advertised by its upstream `PDN` neighbor (once the upstream jack is **Connected**) and forward that head identity on its downstream jack as a chain child.

When a device with a Connected downstream jack receives its own identity as the head advertised on its upstream jack, the chain has closed into a ring:

- The device SHALL latch ring closure as the ring head, clear any foreign adopted head so it continues advertising its own identity downstream, cancel any pending child-to-head roster traffic, and notify its listener of ring closure.
- While latched, the head's own identity returning on its upstream jack refreshes its ring-evidence timestamp.
- **Competing head conflict on a ring**: If a latched ring head receives a different head identity on its upstream jack (from two rings merging or two closure points):
  - If the incoming head identity has a **lower** MAC address than this device, this device SHALL immediately unlatch ring closure and adopt the lower-MAC head.
  - If the incoming head identity has a **higher** MAC address than this device, this device SHALL keep its ring latch as long as its own identity has returned on its upstream jack within the 500 ms ring-evidence window, and SHALL unlatch and adopt the incoming head only if self-returns have ceased for more than 500 ms (indicating a physical break elsewhere in the ring).
- **Ring membership across all members**: Every device on a closed ring SHALL report that it is on a ring — the latching head directly, and every other member by relaying the ring-closed flag downstream while its upstream jack holds a head and its downstream jack is Connected.
- **Ring break**: When either the upstream jack or the downstream jack on a device disconnects, that device SHALL immediately clear any latched ring closure and report that it is no longer on a ring.

#### Scenario: Head identity propagates down an open chain
- **WHEN** devices $A \to B \to C$ are cabled in an open chain with Connected jacks
- **THEN** $A$ acts as the chain head, $B$ and $C$ act as chain children with adopted head $A$, and none of the three is on a ring

#### Scenario: Ring closure latches at the head and relays ring membership
- **WHEN** the tail of chain $A \to B \to C$ connects its downstream jack to $A$'s upstream jack and $A$'s identity returns to $A$
- **THEN** $A$ latches ring closure as the ring head, fires its ring-closed notification, and $A$, $B$, and $C$ all report that they are on a ring

#### Scenario: Simultaneous ring closure resolves to lower MAC head
- **WHEN** two sub-chains headed by $A$ and $B$ ($A < B$) plug into each other simultaneously so both briefly latch ring closure
- **THEN** $B$ unlatches upon receiving $A$'s lower-MAC head advertisement and adopts $A$, while $A$ retains its latch while its own identity keeps returning within 500 ms

#### Scenario: FDN cabled between PDNs does not form a chain link or close a ring
- **WHEN** a `PDN` connects its downstream jack to an `FDN`'s upstream jack (with no other `PDN` neighbors)
- **THEN** the `PDN` reports chain role `STANDALONE` rather than `HEAD`, and cabling through an `FDN` never closes a ring or causes the `FDN` to report `RING`

### Requirement: Roster structural invariants

At all times, every device's chain roster SHALL maintain the following structural invariants:

- **No self-membership**: a device SHALL NOT appear as a downstream member in its own roster.
- **Single child per upstream jack**: because each device has only one downstream jack, at most one member in a roster may be recorded with a given upstream neighbor $u$. Recording a member $d$ with upstream $u$ SHALL evict any other member $m \ne d$ currently recorded with upstream $u$, along with all transitive downstream descendants of $m$.
- **Subtree pruning**: whenever a member is removed from a roster, all members recorded as transitive downstream descendants of that member SHALL also be removed.

#### Scenario: New child on an occupied upstream jack evicts the prior child and its subtree
- **WHEN** a head's roster holds $B \gets A$ and $C \gets B$, and the head applies an announcement that $D$ is now connected to $A$'s downstream jack
- **THEN** $B$ and $B$'s descendant $C$ are evicted, leaving $D \gets A$

#### Scenario: Disconnecting an intermediate node prunes its descendants
- **WHEN** a head's roster holds $B \gets A$, $C \gets B$, and $D \gets C$, and $B$ is removed due to a disconnect on $A$'s downstream jack
- **THEN** $B$, $C$, and $D$ are all removed from the head's roster

### Requirement: Edge authority and stale-event immunity

Each downstream device $d$ SHALL be the sole authority for announcing its incoming link $(d \gets u)$ from its current upstream neighbor $u$ to the head, and each upstream device $u$ SHALL be the sole authority for reporting that its downstream jack disconnected.

Only a device currently acting as a chain or ring head SHALL apply incoming disconnect reports to its roster.

The capability SHALL guarantee that stale or concurrent disconnect reports never erase a currently connected member or resurrect a departed member:

- **Cross-upstream move**: If $d$ disconnects from $u_1$ and connects to $u_2$ ($u_2 \ne u_1$), a disconnect report from $u_1$ arriving at the head after $d$ is already recorded under $u_2$ SHALL NOT remove $d$ or $d$'s subtree.
- **Same-upstream replug**: If $d$ disconnects from $u$ and reconnects to the same $u$ (or another device $c$ connects to $u$), $u$'s prior disconnect report is obsolete upon reconnect and SHALL NOT erase $d$'s subsequent announcement under $u$.
- **Unannounced intermediate disconnect**: If $d$'s own announcement to the head has not yet arrived when $d$ disconnects from $u$, but a descendant $e$ of $d$ is already recorded in the head's roster under $d$, $u$'s disconnect report naming $d$ SHALL still prune $d$'s orphaned descendant $e$.
- **Disconnect across chain merge**: If $d$ disconnects from $u$ before or during a merge of $u$'s chain under a new head $H$, $H$'s roster SHALL NOT retain or re-insert $d$, regardless of whether $u$'s previous head already acknowledged the disconnect or in what order frames from different devices arrive at $H$.
- **Asymmetric link recovery**: If $u$'s downstream jack times out (causing $u$ to report $d$ disconnected to the head) while $d$'s upstream jack stays Connected, and $u$ subsequently re-establishes the wireless context handshake with $d$, $d$ SHALL re-announce its link $(d \gets u)$ to the head so $d$ is restored to the roster.

#### Scenario: Delayed disconnect report after moving to another upstream
- **WHEN** $C$ unplugs from $B$, plugs into $D$, is recorded at head $A$ with upstream $D$, and $B$'s delayed disconnect report for $C$ arrives at $A$ afterward
- **THEN** $A$ retains $C \gets D$ in its roster

#### Scenario: Rapid unplug and replug on the same upstream jack
- **WHEN** $B$ unplugs from $A$'s downstream child $U$ and plugs back into the same $U$ before $U$'s disconnect report was acknowledged by $A$
- **THEN** once $B$ re-announces $B \gets U$ and all in-flight frames settle, $A$'s roster contains $B \gets U$

#### Scenario: Disconnect just before two chains merge
- **WHEN** $C$ unplugs from $B$ in chain $H_2 \to B \to C$ while $H_2$ plugs into the tail of chain $H_1 \to A$
- **THEN** once messages settle, $H_1$'s roster contains `{A, H_2, B}` and does not contain the departed $C$

#### Scenario: One-way upstream link drop and recovery
- **WHEN** $U$'s downstream jack times out and reports $D$ disconnected to head $H$ while $D$'s upstream jack remains Connected, and $U$ then completes a fresh context handshake with $D$
- **THEN** $D$ re-announces $D \gets U$ to $H$ and is restored in $H$'s roster

### Requirement: Superseding and idempotent delivery

For each device and each state-carrying unicast update category (context exchange, link announcement, posse challenger announcement, posse join, or posse confirmation), a newer outbound update to the same recipient SHALL supersede any earlier unacknowledged outbound update of that category to that recipient. Disconnect reports SHALL use distinct retry slots so an intermediate node can forward multiple downstream disconnect reports to the same head concurrently while relying on `Edge authority and stale-event immunity` to make stale or replayed disconnect reports inert.

A recipient SHALL deduplicate retransmissions of an already-applied update from a given sender so that lost acknowledgements never cause an older update to be re-applied after a newer state change.

#### Scenario: Newer update to the same recipient replaces unacknowledged earlier update
- **WHEN** a device queues a unicast update to recipient $T$ and, before $T$ acknowledges it, queues a newer update of the same category to $T$
- **THEN** the earlier unacknowledged update is no longer retransmitted

### Requirement: Quiescent roster convergence

When physical cabling changes cease, all in-flight wireless updates have been delivered or superseded, and serial head advertisements have propagated across every connected hop, the roster of every open-chain head $H$ SHALL contain the exact set of devices physically reachable downstream from $H$, with each member mapped to its current physical upstream neighbor.

#### Scenario: Multi-hop chain merge converges to exact physical topology
- **WHEN** chain $H_1 \to A$ and chain $H_2 \to B \to C$ are cabled together via $A \to H_2$ and all messages settle
- **THEN** $H_1$'s roster is `{A -> H_1, H_2 -> A, B -> H_2, C -> B}`

### Requirement: Cabled game mode arbitration and 1v1 match initiation

When a PDN is in its idle state, physical jack connections SHALL select between cabled game modes according to the following rules and priority:

1. **Shootout takes priority on a closed ring**: While a device is on a closed ring or holds a shootout proposal condition, it SHALL NOT initiate a 1v1 Quickdraw match and SHALL NOT act as a posse challenger or supporter.
2. **1v1 Quickdraw match initiation**: A Hunter PDN that is not on a ring, is a posse challenger (not a supporter), and whose **downstream jack** (opponent jack) is **Connected** to a Bounty PDN SHALL initiate a 1v1 Quickdraw match with that Bounty peer by sending a match-initiation command carrying a newly generated match ID. A Bounty PDN never initiates a 1v1 match; it accepts an incoming match-initiation command when idle, records the match, acknowledges it, and both players enter the match countdown. If the Hunter's match initiation is not acknowledged before the initialization timeout expires, the Hunter clears the pending match and may retry while still Connected.
3. **Posse supporter mode**: A PDN that is not on a ring and whose **opponent jack** is Connected to a PDN of the **same** standing role (Hunter-to-Hunter on the downstream jack, or Bounty-to-Bounty on the upstream jack) SHALL enter the Supporter Ready state.
4. **FDN Symbol match**: A PDN that is not on a ring and whose **downstream jack** is Connected to an `FDN` SHALL enter Symbol Match with that `FDN` (detailed in `openspec/specs/symbol-match/spec.md`).

If the required jack for an active 1v1 match (downstream jack for Hunter, upstream jack for Bounty) disconnects during match countdown or before the match resolves in a non-shootout match, the device SHALL abandon the match and return to idle.

#### Scenario: Closed ring suppresses 1v1 match between adjacent Hunter and Bounty
- **WHEN** a Hunter and a Bounty are cabled into a closed ring with the Hunter's downstream jack connected to the Bounty's upstream jack
- **THEN** the Hunter does not initiate a 1v1 match and both devices enter Shootout proposal instead

#### Scenario: Hunter initiates 1v1 match only after Bounty jack reaches Connected
- **WHEN** a standalone Hunter's downstream jack is cabled to a standalone Bounty's upstream jack
- **THEN** the Hunter waits until the jack reaches **Connected** (context received confirming a Bounty PDN) and then sends match initiation to the Bounty

#### Scenario: Hunter plugged into another Hunter becomes a supporter instead of initiating a match
- **WHEN** Hunter $H_2$'s downstream jack (opponent jack) connects to Hunter $H_1$'s upstream jack (supporter jack) in an open chain
- **THEN** $H_2$ sees a same-role PDN on its opponent jack, becomes a supporter (entering Supporter Ready), and does not initiate a match

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
  - **Supporter confirmation and $+15\text{ ms}$ boost**: When an armed supporter presses a button before the draw window opens, it locks in its confirmation and sends a reliable confirmation message to its challenger (re-sending if its challenger identity updates while the confirmation is standing; leaving the supporter role clears the standing confirmation). Each confirmed supporter currently in the challenger's posse member list grants the challenger a **15 ms draw-time boost** (reducing the challenger's effective draw time in the match).
  - **Draw window open**: When the challenger enters the active draw phase of the match, it broadcasts an unacknowledged draw posse event naming itself; supporters of that challenger disarm their buttons so presses after the draw window opens are ignored.
  - **Match result**: When the challenger finishes the match, it reliably broadcasts a posse win or loss event (superseding any earlier terminal posse event) to its posse member list. Supporters of that challenger acknowledge the event, display the posse win/loss outcome (distinguishing whether the supporter confirmed or missed the countdown) for 8 seconds, and then reset to the posse-ready state.

#### Scenario: Multi-hop Hunter posse enrollment and boost calculation
- **WHEN** Hunters $S_2 \to S_1 \to C_H$ are cabled (`S2.downstream -> S1.upstream`, `S1.downstream -> CH.upstream`) and $C_H$'s downstream jack connects to Bounty challenger $C_B$
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
