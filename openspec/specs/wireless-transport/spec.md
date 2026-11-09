# wireless-transport Specification

## Purpose

All peer-to-peer wireless protocols on PDN and FDN devices (such as `chain-roster`, `shootout`, `quickdraw-match`, `symbol-match`, `player-provisioning`, `match-collection`, and `crash-reporting`) exchange frames over ESP-NOW. The wireless transport capability provides typed unicast channels, broadcast fan-out delivery, sequence-number allocation, retry with exponential backoff, deadline-bounded abandonment, duplicate suppression, and enabling or disabling the peer-to-peer radio on an FDN when Wi-Fi station mode is used (PDNs operate exclusively on the peer-to-peer radio and never use Wi-Fi).

This specification states the required outcomes, invariants, and environmental constraints of the wireless transport layer.

Terms used throughout:

- **Packet type**: a numeric discriminator identifying the protocol channel a wireless frame belongs to.
- **Sequence ID**: an 8-bit unsigned identifier carried in a frame. `0` denotes an unsequenced fire-and-forget frame; `1..255` denote reliable frames requiring delivery confirmation and duplicate suppression.
- **Unicast delivery**: a frame addressed to a single peer MAC address, whose delivery confirmation is the radio link layer's transmit-success verdict.
- **Broadcast fan-out**: a single frame addressed to the broadcast MAC address (`FF:FF:FF:FF:FF:FF`) that is owed a fan-out acknowledgement from a specified list of recipient MAC addresses.
- **Fan-out acknowledgement**: a unicast frame on the dedicated acknowledgement packet type carrying the packet type and sequence ID of the fan-out frame it answers.
- **Superseding mode**: a retry policy in which a newer send on the same packet type to the same recipient replaces any prior unacknowledged send to that recipient on that packet type.
- **Distinct mode**: a retry policy in which multiple sends on the same packet type retain independent retry slots.
- **Fan-out mode**: a channel whose every reliable send is a broadcast fan-out and whose receive path dispatches every frame to the application, which decides admission and answers with the fan-out acknowledgement.

## Requirements

### Requirement: Physical and link-layer constraints

The wireless transport SHALL operate within the following hardware and radio constraints:

- The hardware peer table for unicast encryption/addressing holds at most **20 entries**. Consequently, any fan-out that can address a ring or posse larger than 20 devices SHALL transmit using the single broadcast MAC address rather than one unicast peer slot per recipient.
- Each device has a single outbound transmit queue and drains inbound frames and send-status verdicts sequentially on the main task, so frames from one sender to one receiver are placed on the air in send order, and handlers never race channel teardown.
- A link-layer transmit-success verdict on a unicast frame confirms that the peer's radio acknowledged the 802.11 frame; a transmit-success verdict on a broadcast frame confirms only that the local radio transmitted it and carries no per-recipient delivery evidence.
- A link-layer transmit-failure verdict occurs within milliseconds after hardware MAC retries and SHALL NOT trigger an immediate application retransmit; all retransmissions are paced by the transport backoff timer.
- On an FDN, peer-to-peer wireless communication and Wi-Fi station HTTP requests share one physical radio and cannot transmit simultaneously.

#### Scenario: Link-layer send failure waits for backoff timer
- **WHEN** the radio reports a link-layer transmit failure a few milliseconds after an initial unicast transmission
- **THEN** the transport keeps the frame pending and waits for the 100 ms backoff timer before retransmitting

#### Scenario: Fan-out to a large ring uses broadcast addressing
- **WHEN** a reliable broadcast fan-out is queued for $N$ recipients ($N \ge 1$)
- **THEN** each transmission emits a single frame to `FF:FF:FF:FF:FF:FF` rather than $N$ unicast frames

### Requirement: Sequence ID allocation and boot seeding

Each reliable channel SHALL allocate outbound sequence IDs from a counter that cycles through `1..255` and skips `0` (wrapping from `255` to `1`). Sequence ID `0` SHALL be reserved exclusively for unsequenced fire-and-forget frames.

To prevent a device that resets and transmits its first reliable frame from colliding with the sequence ID `1` it sent just before resetting, the outbound sequence counter SHALL be seeded with a non-zero random byte (`1..255`) at boot.

Retransmissions of an in-flight frame SHALL carry the exact same sequence ID and payload bytes as the initial transmission of that frame.

#### Scenario: Sequence ID wraparound skips zero
- **WHEN** a channel's last allocated sequence ID is `255` and another reliable send is queued
- **THEN** the newly allocated sequence ID is `1`

#### Scenario: Fire-and-forget frame uses sequence ID zero
- **WHEN** an unsequenced frame (sequence ID `0`) is transmitted or received
- **THEN** no retry entry is created on the sender and duplicate suppression is not applied by the transport sequence cursor on the receiver

### Requirement: Single-slot unicast superseding and deduplication

Every unicast state-carrying channel SHALL use superseding mode so that at most one unacknowledged frame per `(packet type, sender, recipient)` is in flight at any time:

- Queuing a new unicast send to recipient $R$ on packet type $P$ SHALL remove $R$ from any prior unacknowledged send on $P$ before arming the new frame.
- On the receiver, each channel SHALL track the last delivered non-zero sequence ID per sender MAC address (for up to 32 concurrent senders, evicting the oldest sender entry when full) without an expiring claim timer, relying on boot-seeded sequence IDs so delayed retransmissions after a temporary radio outage are still suppressed.
- When a frame with a non-zero sequence ID arrives whose length matches the channel's expected payload size:
  - If its sequence ID equals the recorded last sequence ID for that sender MAC, the channel SHALL suppress the duplicate without invoking the application receive callback.
  - Otherwise, the channel SHALL record that sequence ID as the last delivered sequence ID for that sender MAC and invoke the application receive callback.
- When a frame arrives whose byte length does not match the channel's payload size, the channel SHALL log a length mismatch and drop the frame without updating the sender's last sequence ID or invoking the receive callback.

Unicast channels that rely on single-slot sequence-ID deduplication SHALL NOT use distinct retry mode, because concurrent in-flight frames with different sequence IDs from the same sender alternate on retransmission and defeat single-slot deduplication.

#### Scenario: Newer unicast send supersedes unacknowledged prior send
- **WHEN** sender $A$ sends frame $F_1$ (sequence ID `4`) to $B$ on a unicast channel and, before $F_1$ is acknowledged, sends frame $F_2$ (sequence ID `5`) to $B$ on the same channel
- **THEN** $F_1$ is removed from the retry queue and only $F_2$ is retransmitted

#### Scenario: Duplicate unicast retransmission is suppressed
- **WHEN** receiver $B$ delivers sequence ID `4` from $A$ and subsequently receives a retransmitted copy of sequence ID `4` from $A$ with no intervening sequence ID from $A$
- **THEN** $B$ suppresses the retransmission and does not invoke the application handler a second time

#### Scenario: Corrupted or wrong-length frame is rejected
- **WHEN** a frame arrives on a channel with a byte length different from that channel's payload size
- **THEN** the frame is dropped, the sender's recorded sequence ID is unchanged, and the application handler is not called

### Requirement: Broadcast fan-out tracking and per-recipient acknowledgement

A reliable broadcast fan-out SHALL transmit one frame to the broadcast MAC address while tracking a list of expected recipient MAC addresses:

- Queuing a broadcast fan-out with an empty recipient list SHALL send nothing and create no retry entry.
- A recipient answers a fan-out frame with a fan-out acknowledgement sent to the frame's source MAC, naming the frame's packet type and sequence ID. One acknowledgement packet type serves every fan-out channel; the transport routes each acknowledgement to the channel owning the packet type it names.
- When a fan-out acknowledgement arrives from a sender MAC, the transport SHALL remove that sender MAC from the named frame's outstanding recipient list and report delivery to that channel's delivery observer. Once the recipient list becomes empty, the frame's retry entry is retired.
- A fan-out acknowledgement SHALL be honoured only when the channel it names is a fan-out channel and the acknowledging MAC is still owed on the named frame. Frames are unauthenticated, so an acknowledgement naming a unicast channel, an unclaimed packet type, or a recipient not owed SHALL clear nothing and fire no observer.
- A fan-out channel SHALL NOT apply sequence-cursor duplicate suppression on receive: distinct frames interleave on one packet type, and because a broadcast frame produces no per-recipient link-layer delivery evidence, a recipient receiving a duplicate of an already-applied fan-out frame SHALL still emit the fan-out acknowledgement so a lost acknowledgement does not cause the sender to retry to exhaustion. Idempotence against the duplicate belongs to the application, which acknowledges only frames it admits (an acknowledgement is a unicast and spends a hardware peer-table slot on its destination).
- On each retry timer expiry while at least one recipient remains unacknowledged and the frame's deadline has not expired, the transport SHALL emit a single broadcast retransmission of the frame (counting as one retry regardless of how many recipients remain).
- Cancelling a specific recipient MAC on a packet type SHALL remove that recipient MAC from any in-flight unicast or broadcast fan-out on that packet type without firing the abandonment callback.
- Cancelling all sends on a packet type SHALL silently drop that packet type's in-flight entries without firing the abandonment callback. A frame that must outlive a teardown (the frame reporting that a conversation is over) is given a packet type of its own, which the teardown does not cancel.

#### Scenario: Partial acknowledgement narrows remaining recipients without extra frames
- **WHEN** a broadcast fan-out with sequence ID `9` is sent to `{B, C, D}` and $B$ and $C$ send fan-out acknowledgements naming sequence ID `9` before the first retry timer expires
- **THEN** at the 100 ms retry mark one broadcast frame is transmitted for the remaining recipient $D$, and once $D$ acknowledges, retransmission stops

#### Scenario: Forged acknowledgement on a unicast channel clears nothing
- **WHEN** a device has a reliable unicast frame with sequence ID `4` pending to $B$ and receives a fan-out acknowledgement from $B$'s MAC naming that unicast channel and sequence ID `4`
- **THEN** the frame remains pending until the radio's transmit-success verdict, and no delivery observer fires

#### Scenario: Cancelling a tournament's packet types leaves its terminal frame delivering
- **WHEN** a tournament resets and cancels all in-flight frames on its in-progress command packet types while its ABORT or TOURNAMENT_END fan-out is still owed acknowledgements
- **THEN** the in-progress frames are dropped while the terminal frame continues retransmitting to its remaining recipients until they acknowledge or its deadline expires

### Requirement: Exponential backoff and deadline-bounded abandonment

Every reliable send (unicast or broadcast fan-out) SHALL be governed by two timers armed at the initial send:

- **Retry backoff timer**: starts at **100 ms** and doubles on each retry round (`100 ms`, `200 ms`, `400 ms`, `800 ms`, ...), triggering a retransmission whenever it expires before the deadline.
- **Deadline timer**: expires at **1500 ms** measured in wall-clock monotonic time from the initial send.

On each transport tick, the deadline timer SHALL be checked **before** the retry backoff timer so a main task stalled past the 1500 ms span never emits a late retransmission after the deadline has already elapsed.

When the deadline timer expires for an in-flight frame, the transport SHALL remove the frame from the retry queue first and then invoke the abandonment callback once for each recipient still remaining in that frame's recipient list, passing the packet type, sequence ID, recipient MAC, and frame payload. Because the frame is removed before callbacks run, an abandonment callback MAY safely queue new sends or cancel existing sends on the same transport.

#### Scenario: Unreachable recipient is abandoned after 1500 ms deadline
- **WHEN** a reliable send to recipient $B$ receives no acknowledgement across its initial transmission and three backoff rounds (100 ms, 200 ms, 400 ms, 800 ms)
- **THEN** at 1500 ms the entry is removed and the abandonment callback fires once for $B$

#### Scenario: Main-task stall past deadline abandons without emitting a late retry
- **WHEN** an in-flight frame's retry timer and 1500 ms deadline timer have both expired on the same tick
- **THEN** the transport abandons the frame immediately and does not transmit another copy

### Requirement: One transport per device, channel registration, and peer-to-peer radio enable/disable

Each device SHALL own exactly one reliable transport, holding the one retry queue every channel shares. The device loop SHALL pump it once per tick, after the radio driver has delivered that tick's inbound frames and send verdicts and before the application tick, so an abandonment the application reacts to has already fired. No protocol manager pumps retries of its own.

A channel SHALL claim its packet type on the device transport when it is constructed, which also installs the radio's receive and send-verdict handlers for that packet type, and SHALL release the claim, drop its pending sends, and clear those handlers when it is destroyed. Each packet type SHALL be owned by at most one channel at a time: a second claim of a claimed packet type is a wiring defect, SHALL be logged, and SHALL leave the second channel without a radio (it neither sends nor receives) while the first keeps the type.

When an FDN disables the peer-to-peer radio to use Wi-Fi station mode for HTTP server communication, peer-to-peer transmissions are unavailable on that FDN until the peer-to-peer radio is re-enabled. Any reliable frame still in flight while the peer-to-peer radio is disabled remains bounded by its 1500 ms deadline timer so retry queues cannot grow without bound while the peer-to-peer radio is disabled.

#### Scenario: Second claim of a packet type is refused
- **WHEN** a subsystem constructs a channel on packet type $P$ while another channel on this device already holds $P$
- **THEN** the transport logs the collision, the existing channel keeps $P$, and the new channel's sends and acknowledgements go nowhere

#### Scenario: Retries advance without the owning manager ticking
- **WHEN** a channel has a reliable frame pending and its owning manager is never ticked by the mounted app
- **THEN** the frame is still retransmitted on the backoff timer and abandoned at the deadline, because the device loop pumps the transport
