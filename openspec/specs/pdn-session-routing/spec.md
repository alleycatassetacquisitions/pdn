# pdn-session-routing Specification

## Purpose

A PDN runs multiple top-level gameplay and provisioning apps (player provisioning, hub, 1v1 match, multi-player shootout, and FDN symbol matching) that share underlying peer-to-peer radio state, posse membership, match storage, and physical jack coordination. A PDN never connects to Wi-Fi; instead, it communicates with peer PDNs and with FDNs over serial jacks and the peer-to-peer radio. The `pdn-session-routing` capability defines the boot splash hold, top-level app registration, background session tick, hub awaken and idle screen behavior, hub idle outgoing transition priority, posse supporter UI lifecycle, and post-match sleep cooldown routing.

This specification states the required outcomes, invariants, and environmental constraints of PDN session management and cross-app routing.

Terms used throughout:

- **Hub app**: the central PDN app comprising the awaken sequence, idle state, posse supporter-ready state, and post-game sleep cooldown state.
- **Session pump**: the background per-tick handler that advances posse, match, and shootout transport timers regardless of which app or state is currently mounted.
- **Followed challenger**: the MAC address of the posse head that a chained supporter is currently tracking.

## Requirements

### Requirement: Boot splash hold and top-level app registration

At boot, before entering gameplay apps, the PDN SHALL display the faction boot splash and register its top-level apps without initiating any Wi-Fi connection:

- The PDN SHALL render the boot splash screen and hold for `3000 ms`.
- After the splash hold, the PDN SHALL register its top-level apps — player provisioning, hub, match, shootout, and symbol — and mount the initial provisioning app (`player-provisioning`).
- When player provisioning completes (after the `5000 ms` welcome hold in the provisioning app), the PDN SHALL transition into the hub app, which enters via its awaken sequence before mounting the hub idle state.

#### Scenario: Boot splash holds for 3000 ms before mounting initial provisioning app
- **WHEN** a PDN completes hardware and crash-reporting initialization at boot
- **THEN** the splash screen is displayed for `3000 ms` without starting Wi-Fi, after which the initial provisioning app is mounted

### Requirement: Background session pump across all apps

The PDN SHALL advance all wireless gameplay transport state machines on every device main-loop tick regardless of which app or state is currently mounted:

- On every main-loop tick, the session pump SHALL execute the periodic retry and timeout checks for posse membership, 1v1 match result replication, and shootout result replication.

#### Scenario: Retry machines advance while non-hub app is mounted
- **WHEN** a PDN is mounted in the match, shootout, or symbol app with an unacknowledged posse or result packet in flight
- **THEN** the session pump continues advancing the posse, match, and shootout retry timers on every device tick

### Requirement: Hub awaken sequence and idle screen behavior

The hub app SHALL enter via the awaken sequence before mounting the idle state:

- On entry to the awaken sequence, the PDN SHALL display the player's allegiance artwork, start the haptic motor at medium intensity (`125`), and step the haptic motor every `100 ms` for `20` steps (`2000 ms` total, alternating full intensity `255` and off `0` on each step) before transitioning to the idle state and turning the haptic motor off.
- On mounting the idle state, the PDN SHALL ensure the peer-to-peer radio is enabled, start the role-appropriate (`Hunter` or `Bounty`) idle LED animation, and display the player's allegiance artwork alongside the current statistics screen.
- While in the idle state, clicking either the primary button or the secondary button SHALL cycle forward through eight screens (`0` through `7`, wrapping from `7` back to `0`):
  - `0`: Total wins (`"Wins"`)
  - `1`: Current win streak (`"Streak"`)
  - `2`: Total losses (`"Losses"`)
  - `3`: Total matches played (`"Matches"`)
  - `4`: Last reaction time in milliseconds (`"Last"` / `"Reaction"`)
  - `5`: Average reaction time in milliseconds (`"Average"` / `"Reaction"`)
  - `6`: Current posse supporter count (`"Posse"`)
  - `7`: Player symbol glyph

#### Scenario: Clicking primary or secondary button cycles forward through 8 idle screens
- **WHEN** a PDN is mounted in the hub idle state on screen `0` and the user clicks the primary or secondary button eight times
- **THEN** the display advances sequentially through screens `1` through `7` and wraps back to screen `0`

### Requirement: Hub idle outgoing transition priority

On every main-loop pass while in the hub idle state, the PDN SHALL evaluate outgoing transitions in strict priority order so that multi-device ring topologies and active matches are never preempted by lower-priority states:

1. **Shootout proposal**: If the shootout subsystem reports that this PDN should enter a shootout proposal (a closed multi-PDN ring has formed with mixed roles), the PDN SHALL transition to the shootout app's proposal state. This transition SHALL take precedence over a 1v1 match or posse supporter transition so a closed ring is never demoted to a 1v1 match.
2. **1v1 match countdown**: Otherwise, if a 1v1 match has completed its role and player handshake on the player's opponent jack (downstream jack for a Hunter, upstream jack for a Bounty), the PDN SHALL transition to the match app's countdown state. While a Hunter is waiting for a newly initiated downstream-jack match handshake to complete, the PDN SHALL enforce a `1000 ms` initialization timeout before clearing a stalled handshake.
3. **Posse supporter**: Otherwise, if the PDN is currently a chained posse supporter, the PDN SHALL transition to the hub supporter-ready state.
4. **Shootout aborted**: Otherwise, if a shootout was aborted, the PDN SHALL transition to the shootout app's aborted state.
5. **FDN symbol match**: Otherwise, if no 1v1 match handshake is in progress and an FDN peer is Connected on a serial jack, the PDN SHALL transition to the symbol app.

#### Scenario: Closed mixed-role ring takes precedence over 1v1 match initiation
- **WHEN** a PDN in the hub idle state simultaneously satisfies shootout proposal entry and 1v1 opponent-jack match readiness
- **THEN** the PDN transitions to the shootout app rather than the 1v1 match app

#### Scenario: Stalled 1v1 match handshake clears after 1000 ms
- **WHEN** a Hunter PDN in the hub idle state initiates a 1v1 match handshake on its downstream jack and the peer does not complete the handshake within `1000 ms`
- **THEN** the stalled match instance is cleared and the PDN remains in the hub idle state

### Requirement: Posse supporter event routing and UI lifecycle

When the PDN is a posse supporter and receives wireless posse game-event broadcasts (`COUNTDOWN`, `DRAW`, `WIN`, or `LOSS`):

- The PDN SHALL ignore any posse game-event broadcast unless both the wireless sender MAC address and the payload's challenger MAC address equal the supporter's followed challenger MAC.
- If the PDN ceases to be a posse supporter while in the supporter-ready state, it SHALL turn off any active haptics and transition back to the hub idle state.
- Prior to a countdown, the button SHALL be disarmed and the display SHALL show `"Posse"` / `"Ready"`.
- On a `COUNTDOWN` event from the followed challenger, the PDN SHALL clear any standing supporter confirmation or prior result, arm the primary and secondary buttons, and display `"Posse"` / `"PRESS"`, so a supporter who confirmed during an earlier aborted countdown must press again for the new countdown.
- If the user clicks the primary or secondary button while armed, the PDN SHALL transmit a posse confirmation to the challenger, mark the supporter confirmed, and display `"Locked"` / `"In"`.
- On a `DRAW` event from the followed challenger, the PDN SHALL disarm the buttons.
- On a `WIN` or `LOSS` event from the followed challenger, the PDN SHALL acknowledge the event to the challenger MAC (when the event carries a non-zero sequence ID), disarm the buttons, display `"Posse"` (if the supporter confirmed) or `"Missed"` (if the supporter did not confirm) paired with `"Won"` or `"Lost"`, and start an `8000 ms` result-clear timer.
- When the `8000 ms` result-clear timer expires, the PDN SHALL clear both the displayed result and the confirmation flag, returning to `"Posse"` / `"Ready"`.

#### Scenario: Posse event from an unrelated or spoofed sender is ignored
- **WHEN** a supporter following challenger `AA:BB:CC:DD:EE:01` receives a `COUNTDOWN` broadcast whose sender MAC or challenger MAC is `AA:BB:CC:DD:EE:99`
- **THEN** the event is ignored and the supporter's state and confirmation status remain unchanged

#### Scenario: Supporter confirms during countdown and resets 8 seconds after challenger win
- **WHEN** a mounted supporter receives `COUNTDOWN` from its followed challenger, clicks the primary button, receives `WIN`, and waits `8000 ms`
- **THEN** the display shows `"Posse"` / `"PRESS"` on countdown, `"Locked"` / `"In"` on click, acknowledges the `WIN` and shows `"Posse"` / `"Won"`, and resets to `"Posse"` / `"Ready"` with confirmation cleared after `8000 ms`

#### Scenario: Supporter who misses countdown window sees Missed result
- **WHEN** a mounted supporter receives `COUNTDOWN`, does not press a button before `DRAW`, and then receives `LOSS`
- **THEN** the button is disarmed on `DRAW` and the display shows `"Missed"` / `"Lost"` for `8000 ms`

### Requirement: Post-match sleep cooldown and match cleanup

After a 1v1 match or multi-player shootout finishes, the PDN SHALL route through the hub sleep cooldown state before re-entering the hub idle state:

- In the match app, after the `8000 ms` win or lose result hold (`quickdraw-match`), the PDN SHALL transition to the hub sleep cooldown state.
- Dismounting the match app SHALL always turn the haptic motor off and clear the active match state.
- In the shootout app, when the final-standings state exits (after the physical ring opens), the PDN SHALL transition to the hub sleep cooldown state rather than directly to the idle state.
- The hub sleep cooldown state SHALL display the player's allegiance artwork for `60,000 ms` (`60 seconds`) before transitioning through the awaken sequence back to the idle state, providing a cooldown window after a completed match or shootout.

#### Scenario: Standalone match result holds for 8 seconds and enters 60-second sleep cooldown
- **WHEN** a PDN finishes a standalone 1v1 match and completes the `8000 ms` win or lose state hold
- **THEN** the match app turns off the haptic motor and transitions to the hub sleep cooldown state for `60,000 ms` before running the awaken sequence and returning to the idle state

#### Scenario: Shootout final standings routes through sleep cooldown on unplug
- **WHEN** a PDN in the shootout final-standings state detects that the physical ring has opened
- **THEN** the PDN transitions to the hub sleep cooldown state for `60,000 ms` before returning to the awaken sequence and idle state
