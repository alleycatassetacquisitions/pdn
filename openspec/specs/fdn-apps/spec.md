# fdn-apps Specification

## Purpose

An FDN (Fixed Data Node) is a stationary interactive installation and base station that bridges PDNs (which have no Wi-Fi connectivity and communicate exclusively with peer PDNs and FDNs over serial jacks and the peer-to-peer radio) to the central server. An FDN performs four unified roles:

1. **Cabled background base-station services**: Whenever a PDN is Connected on an FDN serial jack, the FDN propagates its synchronized UTC wall-clock timestamp to the PDN (`http-date-clock-sync`) and collects any pending match records (`match-collection`) and crash records (`crash-reporting`) over the peer-to-peer radio into bounded non-volatile spools on the FDN.
2. **Player provisioning (`player-provisioning`)**: When an unprovisioned PDN (`nil` 16-byte player UUID / `0xFFFF` wire sentinel) is Connected on an FDN serial jack and a player scans their credential at the FDN, the FDN transitions to Wi-Fi station mode, resolves the player's profile from the server (`GET /device/players/by-pin/{pin}`), re-enables the peer-to-peer radio, and delivers the provisioned profile to the cabled PDN.
3. **Interactive FDN proximity and symbol-match gameplay (`symbol-match`)**: The FDN beacons its identity over the peer-to-peer radio, reacts to nearby PDNs with signal-strength-scaled visual feedback, and hosts the dual-jack symbol-matching hacking puzzle (`symbol-match`) that reveals the unit's fixed codeword on win.
4. **Periodic Wi-Fi server synchronization**: While idle on a periodic timer, the FDN switches to Wi-Fi station mode to batch-upload collected PDN match records (`POST /quickdraw-matches`) and local/relayed crash logs (`POST /device-logs`), updates its UTC wall clock from the server's HTTP `Date` response header (`http-date-clock-sync`), and restores the peer-to-peer radio.

This specification states the required outcomes, invariants, and environmental constraints of the unified FDN application suite.

## Requirements

### Requirement: Boot sequence, periodic FDN beacon, and app registration

At boot, the FDN SHALL display its splash screen, begin periodic peer-to-peer identity broadcasts, and mount its home app:

- At boot, the FDN SHALL display `"ALLEYCAT"` for `2000 ms` while allowing any boot-time crash report upload to progress.
- After the splash hold, the FDN SHALL broadcast a wireless FDN player-info beacon every `12,000 ms` whenever the peer-to-peer radio is enabled.
- The FDN SHALL register its top-level apps (`idle` and `symbol-match`), mount `idle` as the home app, and return to `idle` when connected PDNs unplug from `symbol-match`.

#### Scenario: FDN displays 2000 ms splash and starts 12-second beacon at boot
- **WHEN** an FDN completes hardware initialization at boot
- **THEN** `"ALLEYCAT"` is displayed for `2000 ms`, the `12,000 ms` periodic wireless FDN player-info broadcast starts, and the home app is mounted

### Requirement: Cabled background services on any Connected PDN

Whenever a PDN reaches the **Connected** state on either of the FDN's upstream serial jacks:

- If the FDN's UTC wall clock has been synchronized (`>= 2025-01-01T00:00:00Z`), the FDN SHALL propagate its current UTC epoch milliseconds to the Connected PDN (`http-date-clock-sync`).
- While the peer-to-peer radio is enabled, the FDN SHALL accept and spool pending standalone match records (`match-collection`) and pending crash records (`crash-reporting`) from the verified MAC address of the Connected PDN, acknowledging each record only after it is staged in the FDN's bounded non-volatile spool.

#### Scenario: Connected PDN receives UTC time and transfers pending match and crash records
- **WHEN** a provisioned PDN with uncollected match and crash records connects to an FDN whose UTC clock is synchronized
- **THEN** the FDN propagates its UTC epoch milliseconds to the PDN and ingests and acknowledges the PDN's pending match and crash records into the FDN's non-volatile spools

### Requirement: Idle screen and RSSI-tiered proximity detection

While mounted in the `idle` app's default idle state, the FDN SHALL listen for wireless PDN proximity broadcasts, serial-jack PDN connections, and periodic server-upload timer expirations:

- On mounting the idle state, the FDN SHALL clear its LEDs and display `"ALLEYCAT"`.
- When the FDN receives a wireless player-info broadcast from a PDN while in the idle state, it SHALL transition to the player-detected state, display `"PLAYER"` / `"NEARBY"`, and run a blue vertical-chase LED animation whose brightness and duration are scaled by the last received RSSI tier:
  - **Strong (`RSSI > -50 dBm`)**: brightness `255`, duration `5000 ms`
  - **Medium (`-65 dBm < RSSI <= -50 dBm`)**: brightness `150`, duration `2500 ms`
  - **Weak (`RSSI <= -65 dBm`)**: brightness `80`, duration `2500 ms`
- When the player-detected timer expires without a serial-jack connection, the FDN SHALL transition back to the idle state.
- If a PDN establishes a Connected link on a serial jack while in the idle or player-detected state, the FDN SHALL route the cabled session based on the connected PDN's provisioning status.

#### Scenario: Strong proximity signal triggers 5-second full-brightness alert
- **WHEN** an FDN in the idle state receives a wireless player-info packet at `-42 dBm`
- **THEN** the FDN enters the player-detected state, displays `"PLAYER"` / `"NEARBY"` with brightness `255` for `5000 ms`, and returns to the idle state when the timer expires

#### Scenario: Weak proximity signal triggers 2.5-second dim alert
- **WHEN** an FDN in the idle state receives a wireless player-info packet at `-72 dBm`
- **THEN** the FDN enters the player-detected state, displays `"PLAYER"` / `"NEARBY"` with brightness `80` for `2500 ms`, and returns to the idle state when the timer expires

### Requirement: Connected PDN provisioning vs. symbol-match routing

When a PDN connects to the FDN on an upstream serial jack, the FDN SHALL inspect the cabled PDN's connection context from the verified MAC address of that jack and route between player provisioning and the symbol-match hacking game:

- **Unprovisioned PDN (`nil` player UUID / `0xFFFF` wire sentinel)**:
  - While an unprovisioned PDN is Connected on an FDN serial jack and a player presents their credential at the FDN, the FDN SHALL execute the player-provisioning lookup and delivery flow (`player-provisioning`): switching to Wi-Fi station mode to fetch the player's protobuf profile from `GET /device/players/by-pin/{pin}`, updating the FDN's UTC wall clock from the HTTP `Date` header (`http-date-clock-sync`), re-enabling the peer-to-peer radio, and delivering the resolved profile (or lookup failure) to the cabled PDN over the provisioning channel.
- **Provisioned PDN (non-nil player identity)**:
  - When a provisioned PDN is Connected on an FDN serial jack, the FDN SHALL transition to the `symbol-match` app (`symbol-match`) so connected players can play the dual-jack symbol-matching hacking puzzle and reveal the FDN's codeword.
  - When no provisioned PDN remains Connected on either upstream serial jack (outside the 30-second codeword success hold), the FDN SHALL return to the `idle` app.

#### Scenario: Unprovisioned PDN connected to FDN is provisioned on credential scan
- **WHEN** an unprovisioned PDN (`0xFFFF` sentinel / nil UUID) is Connected on an FDN serial jack and the player scans a valid credential at the FDN
- **THEN** the FDN fetches the player profile over Wi-Fi, re-enables the peer-to-peer radio, and delivers the profile to the cabled PDN (`player-provisioning`)

#### Scenario: Provisioned PDN connected to FDN routes to symbol-match app
- **WHEN** a provisioned PDN connects to an FDN serial jack
- **THEN** the FDN mounts the `symbol-match` app for puzzle play

### Requirement: Periodic Wi-Fi uplink for collected matches and crash logs

Collected PDN match records (`match-collection`) and local or relayed PDN crash records (`crash-reporting`) SHALL be persisted in non-volatile storage and uploaded periodically to the central server over Wi-Fi:

- While in the idle state with no PDN actively connected, the FDN SHALL run a `300,000 ms` (5-minute) upload check timer. When the timer expires and the FDN has at least one unuploaded collected match record (`match-collection`) or unuploaded crash record (`crash-reporting`), the FDN SHALL transition to the upload state.
- On mounting the upload state, the FDN SHALL explicitly transition the wireless subsystem into Wi-Fi HTTP station mode (disabling the peer-to-peer radio and initiating Wi-Fi station association with the configured access point), run a `500 ms` glyph loading sequence followed by displaying `"SYNCING"` with a green chase LED animation, and start a `15,000 ms` fallback timeout timer.
- While Wi-Fi HTTP mode is active, background peer-to-peer send attempts SHALL be rejected without automatically switching the wireless subsystem out of Wi-Fi mode or aborting in-flight HTTP requests.
- Once queued for the upload cycle:
  - Pending collected match records SHALL be uploaded in protobuf batches via `POST /quickdraw-matches` (`match-collection`).
  - Pending local and relayed PDN crash records SHALL be uploaded via `POST /device-logs` (`crash-reporting`).
- The FDN SHALL distinguish a completed upload cycle from an unreachable Wi-Fi/server uplink: when all queued upload requests complete within the window, the FDN SHALL re-enable the peer-to-peer radio and return to the idle state; if the `15,000 ms` fallback timeout expires or the HTTP requests fail before all pending records are confirmed, the FDN SHALL cancel any remaining in-flight HTTP requests, surface an upload failure indication, retain unconfirmed records in non-volatile storage for the next 5-minute cycle, re-enable the peer-to-peer radio, and return to the idle state.

#### Scenario: Five-minute idle timer uploads pending match and crash records and restores peer-to-peer radio
- **WHEN** the `300,000 ms` idle upload timer expires on an FDN holding collected PDN match records and pending crash logs, and the server responds with HTTP 2xx/204
- **THEN** the FDN transitions to Wi-Fi HTTP mode, uploads the pending match batch to `POST /quickdraw-matches` and crash logs to `POST /device-logs`, retires the confirmed records from non-volatile storage, re-enables the peer-to-peer radio, and returns to the idle state

#### Scenario: Upload fallback timeout cancels in-flight HTTP, preserves pending records, and restores peer-to-peer radio
- **WHEN** the upload state is mounted with pending match or crash records and Wi-Fi or the server does not respond within `15,000 ms`
- **THEN** the `15,000 ms` fallback timer expires, any in-flight HTTP requests are cancelled, an upload failure indication is surfaced, unconfirmed records remain persisted for the next 5-minute cycle, the peer-to-peer radio is re-enabled, and the FDN returns to the idle state

### Requirement: Registration screen for a cabled unprovisioned PDN

When a PDN whose connection context shows no applied profile (unprovisioned, as distinct from a demo player) becomes Connected on an FDN serial jack while the FDN is in the idle or player-detected state, the FDN SHALL show a registration screen ahead of any other cabled flow:

- While waiting for a lookup request, the FDN SHALL display `"REGISTER"` / `"ENTER CODE"`.
- While a lookup for that PDN is in progress, the FDN SHALL display `"LOOKING"` / `"UP"`.
- On a delivered profile, the FDN SHALL display `"WELCOME"` and the player's display name for `3000 ms`; on a lookup failure or timeout, `"UNABLE TO"` / `"LOCATE ASSET"` for `3000 ms`.
- The FDN SHALL end a lookup that has not answered within `8000 ms` as a lookup failure.
- Outside the registration screen, the FDN SHALL drop a lookup request while a registered PDN is Connected on an FDN jack, so a lookup's Wi-Fi excursion never interrupts that PDN's symbol-match game.
- The FDN SHALL leave the registration screen for the idle state when the PDN is unplugged, when a result has been shown, or, while waiting for a request, when the cabled PDN's context shows an applied profile.

#### Scenario: Unprovisioned PDN plugged in shows the registration screen
- **WHEN** an unprovisioned PDN becomes Connected on an idle FDN
- **THEN** the FDN displays `"REGISTER"` / `"ENTER CODE"` instead of entering the symbol-match flow

#### Scenario: Successful lookup shows the player's name
- **WHEN** the FDN delivers a profile named `"Ada"` to the cabled PDN
- **THEN** the FDN displays `"WELCOME"` / `"Ada"` for `3000 ms` and returns to the idle state
