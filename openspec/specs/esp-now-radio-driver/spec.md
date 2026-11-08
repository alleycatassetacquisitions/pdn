# esp-now-radio-driver Specification

## Purpose

PDN and FDN devices use connectionless 802.11 vendor-action frames (ESP-NOW) for low-latency peer-to-peer broadcasts and unicast game traffic (posse heartbeats, game events, match and shootout result replication, FDN proximity beacons, PDN to FDN data exchange, and wireless symbol matching). PDNs operate exclusively on the peer-to-peer radio and never associate with Wi-Fi access points, while FDNs may temporarily disable the peer-to-peer radio when communicating with the central server. The `esp-now-radio-driver` capability defines the frame size bounds, radio enable/disable lifecycle, fixed channel pinning, single-slot non-blocking transmit pipeline, bounded deferred receive queue, and unicast peer-table eviction.

This specification states the required outcomes, invariants, and environmental constraints of the peer-to-peer radio driver.

Terms used throughout:

- **Fixed peer-to-peer channel**: the configured 2.4 GHz 802.11 channel (`6`) on which all peer-to-peer broadcast and unicast traffic operates.
- **In-flight slot**: the single outbound frame currently owned by the radio transmit pipeline awaiting link-layer completion.
- **Deferred receive queue**: the memory-bounded buffer transferring received frames from the radio interrupt/task context to the main device loop.

## Requirements

### Requirement: Frame payload bounds and disabled-radio rejection

The peer-to-peer radio driver SHALL enforce frame size limits and reject outbound traffic while disabled:

- Every peer-to-peer frame SHALL consist of a fixed 1-byte header (carrying the 1-byte packet type, kept frozen at 1 byte for over-the-air compatibility with older builds) followed by up to `1469 bytes` (`1470` bytes total ESP-NOW v2.0 frame size minus the 1-byte header) of payload.
- An outbound send request whose payload length exceeds `1469 bytes` SHALL be rejected immediately without enqueueing.
- An outbound send request made while the peer-to-peer radio is disabled, or when the bounded outbound send queue is at capacity, SHALL be rejected immediately rather than held while the radio is down or allowed to grow without bound.

#### Scenario: Oversized payload is rejected immediately
- **WHEN** a caller requests transmission of a payload larger than `1469 bytes`
- **THEN** the radio driver rejects the request immediately and does not enqueue the frame

#### Scenario: Send while peer-to-peer radio is disabled is rejected without queuing
- **WHEN** a caller requests transmission of a peer-to-peer frame while the peer-to-peer radio is disabled
- **THEN** the radio driver rejects the request immediately and does not hold the frame for later transmission

### Requirement: Radio enable sequence and verified channel pinning

When enabling the peer-to-peer radio (at boot on a PDN or FDN, or when an FDN re-enables the peer-to-peer radio after a server upload), the driver SHALL guarantee that the radio operates in station mode with power-save disabled and is verified to be on the fixed peer-to-peer channel:

- Enabling the peer-to-peer radio SHALL ensure the 802.11 radio is initialized and started in station (`STA`) mode, disassociate any active or connecting access-point state without stopping the radio, and disable radio power-save so link-layer unicast acknowledgements are not missed between sleep intervals.
- The driver SHALL register the broadcast MAC address (`FF:FF:FF:FF:FF:FF`) in the hardware peer table on initialization.
- The driver SHALL set the primary radio channel to the fixed peer-to-peer channel (`6`) and verify the channel via a hardware readback rather than trusting the setter return code alone.
- If an in-flight background station scan or association attempt prevents the channel change from taking effect immediately (hardware readback does not match the fixed channel), the driver SHALL mark the channel pin as pending and retry every `100 ms` on the main-loop tick (re-issuing a station disassociation before each attempt) until the hardware readback confirms the fixed channel or the peer-to-peer radio is disabled.

#### Scenario: Enabling peer-to-peer radio from a non-peer-to-peer channel restores and verifies channel 6
- **WHEN** an FDN finishes a server upload on channel 11 (or a device boots with the radio initialized on default channel 1) and enables the peer-to-peer radio
- **THEN** the driver disassociates any station connection, disables radio power-save, sets the channel to `6`, and verifies via hardware readback that the operating channel is `6`

#### Scenario: Blocked channel change retries every 100 ms until hardware readback matches
- **WHEN** enabling the peer-to-peer radio encounters an in-flight background scan that leaves the hardware channel readback on a non-peer-to-peer channel
- **THEN** the driver retries the station disassociation and channel pin every `100 ms` on the main loop until the hardware readback matches the fixed peer-to-peer channel (`6`)

### Requirement: Single-slot non-blocking transmit pipeline and main-loop ownership

All outbound frame claiming, completion handling, and send-status notifications SHALL execute exclusively on the main-loop task:

- Requesting transmission of a frame SHALL enqueue a copy of the frame into a bounded outbound queue and return without blocking on link-layer transmission completion.
- At most one outbound frame SHALL occupy the active in-flight slot at any time.
- The radio task's low-level send callback SHALL record only the link-layer completion verdict and return immediately without mutating the outbound queue or invoking higher-level send-status handlers.
- On each main-loop pass, the driver SHALL consume any recorded completion verdict (or expire the in-flight slot if no low-level send callback arrives within a `2000 ms` watchdog window) before claiming the next queued frame:
  - The driver SHALL clear the in-flight slot before invoking the registered send-status handler for that packet type with a boolean delivery verdict (`true` when the link layer reported transmission/delivery success, `false` when the link layer reported failure, synchronous refusal, or watchdog timeout), so a handler that disables the radio or enqueues new traffic can safely run without re-entrancy hazards.
  - Link-layer transmit failures SHALL be reported immediately to the transport layer without blocking the single in-flight slot across multi-tick driver retries, allowing the transport layer's backoff timer (`100 ms`) to pace retransmissions without head-of-line blocking unrelated peers.

#### Scenario: In-flight slot is cleared before invoking send-status handler on main loop
- **WHEN** the link layer reports completion of the active in-flight frame
- **THEN** on the next main-loop pass the driver clears the in-flight slot first, invokes the packet type's send-status handler with `true` (on success) or `false` (on failure), and then claims the next queued outbound frame

#### Scenario: Missed low-level send callback recovers via in-flight watchdog
- **WHEN** an outbound frame occupies the in-flight slot and no low-level send callback arrives within `2000 ms`
- **THEN** the driver clears the stalled in-flight slot, reports `false` to the send-status handler, and resumes draining the outbound queue

### Requirement: Clean state reset on radio disable

When the peer-to-peer radio is disabled:

- The driver SHALL cancel any pending channel-pin retry, discard all queued outbound frames, deinitialize the ESP-NOW stack, and unconditionally discard the active in-flight frame and any pending completion verdict.
- Discarding the in-flight slot on disable SHALL ensure that a missing or late link-layer send callback during teardown never wedges subsequent sends or misattributes a stale verdict to a frame sent after the radio is re-enabled.

#### Scenario: Disabling radio with an in-flight frame does not wedge future sends
- **WHEN** the peer-to-peer radio is disabled while an outbound frame is still in flight and the radio is later re-enabled
- **THEN** the stale in-flight frame and any pending verdict are discarded on disable, and new frames queued after re-enable are transmitted normally

### Requirement: Bounded deferred receive queue and RSSI capture

Inbound peer-to-peer frames SHALL be validated, tagged with signal strength, and dispatched on the main-loop task:

- When a frame arrives on the radio task, the driver SHALL verify that the frame is at least the 1-byte header size (`1` to `1470` bytes total) and that the packet type is within the valid packet type range; malformed frames SHALL be dropped immediately.
- Valid frames SHALL be copied into a deferred receive queue bounded at `16,384 bytes` of queued payload. If enqueueing a frame would cause queued payload bytes to exceed `16,384 bytes`, the frame SHALL be dropped and counted for a main-loop warning log rather than exhausting internal heap memory.
- For every accepted frame, the driver SHALL update the last received RSSI (in dBm) and record the sender's RSSI in a capacity-bounded per-sender table (evicting the oldest entry when full so randomized source MACs cannot exhaust internal memory).
- On each main-loop pass, the driver SHALL drain queued frames in arrival order and invoke the registered packet handler for each frame without holding internal receive-queue locks across the callback.

#### Scenario: Burst exceeding 16 KiB receive cap drops excess frames safely
- **WHEN** a burst of inbound peer-to-peer frames arrives faster than the main loop drains them and total queued payload bytes reach `16,384 bytes`
- **THEN** additional frames arriving before the next main-loop pass are dropped, a drop counter is incremented for logging on the main loop, and internal RAM is not exhausted

#### Scenario: Valid inbound frame updates RSSI and dispatches on main loop
- **WHEN** a valid peer-to-peer frame arrives with receive metadata reporting `-48 dBm`
- **THEN** the driver records `-48 dBm` for the sender and last-received RSSI, and dispatches the payload and sender MAC to the registered packet handler on the next main-loop pass

### Requirement: Unicast peer table management and eviction

Before transmitting a unicast frame to a destination MAC address (any address other than `FF:FF:FF:FF:FF:FF`):

- If the destination MAC is already registered in the hardware peer table, the driver SHALL transmit the frame directly.
- If the destination MAC is not yet registered and the hardware peer table has reached its 20-peer capacity, the driver SHALL evict an existing unicast peer (never evicting the broadcast peer `FF:FF:FF:FF:FF:FF`) before registering the new destination MAC on the fixed peer-to-peer channel (`6`) without link-layer encryption.

#### Scenario: Unicast send to a 21st distinct MAC evicts an existing unicast peer
- **WHEN** the hardware peer table already holds the broadcast peer plus 19 unicast peers (20 total) and a unicast frame is sent to a new MAC address
- **THEN** the driver evicts an existing unicast peer, registers the new destination MAC on the fixed peer-to-peer channel (`6`), and transmits the frame while preserving the broadcast peer
