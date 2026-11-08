# fdn-apps Specification

## Purpose

An FDN (Fixed Data Node) is a stationary interactive installation and base station that bridges PDNs (which have no Wi-Fi connectivity and communicate exclusively with peer PDNs and FDNs over serial jacks and the peer-to-peer radio) to the central server. An FDN performs four unified roles:

1. **Cabled background base-station services**: Whenever a PDN is Connected on an FDN serial jack, the FDN propagates its synchronized UTC wall-clock timestamp to the PDN (`http-date-clock-sync`) and collects any pending match records (`match-collection`) and crash records (`crash-reporting`) over the peer-to-peer radio into bounded non-volatile spools on the FDN.
2. **Player provisioning (`player-provisioning`)**: When an unprovisioned PDN (`nil` 16-byte player UUID / `0xFFFF` wire sentinel) is Connected on an FDN serial jack and a player scans their credential at the FDN, the FDN transitions to Wi-Fi station mode, resolves the player's profile from the server (`GET /device/players/...`), re-enables the peer-to-peer radio, and delivers the provisioned profile to the cabled PDN.
3. **Interactive FDN gameplay and lore (`symbol-match`, `idle`, `main-menu`, and `hacking`)**: The FDN beacons its identity over the peer-to-peer radio, reacts to nearby PDNs with signal-strength-scaled visual feedback, authenticates provisioned PDNs connected over a serial jack against persistent local hack records, hosts the dual-jack symbol-matching puzzle (`symbol-match`), and displays codeword / passphrase lore screens.
4. **Periodic Wi-Fi server synchronization**: While idle on a periodic timer, the FDN switches to Wi-Fi station mode to batch-upload collected PDN match records (`POST /quickdraw-matches`), crash logs (`POST /device-logs`), and pending FDN hack records (`/api/boxes`), updates its UTC wall clock from the server's HTTP `Date` response header (`http-date-clock-sync`), and restores the peer-to-peer radio.

This specification states the required outcomes, invariants, and environmental constraints of the unified FDN application suite.

Terms used throughout:

- **Box ID**: the numeric identifier assigned to an FDN installation.
- **Hack status**: the per-player persistent state recorded in the FDN's non-volatile storage (`0` = not hacked, `1` = hacked locally and pending server upload, `2` = hacked and confirmed uploaded to the server).
- **Authorized PDN**: a connected provisioned PDN whose player identity has a recorded hack status of `1` or `2` on this FDN.

## Requirements

### Requirement: Boot sequence, periodic FDN beacon, and app registration

At boot, the FDN SHALL display its splash screen, begin periodic peer-to-peer identity broadcasts, and mount its home app:

- At boot, the FDN SHALL display `"ALLEYCAT"` for `2000 ms` while allowing any boot-time crash report upload to progress.
- After the splash hold, the FDN SHALL broadcast a wireless FDN player-info beacon every `12,000 ms` whenever the peer-to-peer radio is enabled.
- The FDN SHALL register its top-level apps (`idle`, `symbol-match`, `main-menu`, and `hacking`), mount `idle` as the home app, and return to `idle` when a connected PDN unplugs from `main-menu` or `hacking` (or when `symbol-match` yields back to `idle`).

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
- If a PDN establishes a Connected link on a serial jack while in the idle or player-detected state, the FDN SHALL transition to the connection-detected state.

#### Scenario: Strong proximity signal triggers 5-second full-brightness alert
- **WHEN** an FDN in the idle state receives a wireless player-info packet at `-42 dBm`
- **THEN** the FDN enters the player-detected state, displays `"PLAYER"` / `"NEARBY"` with brightness `255` for `5000 ms`, and returns to the idle state when the timer expires

#### Scenario: Weak proximity signal triggers 2.5-second dim alert
- **WHEN** an FDN in the idle state receives a wireless player-info packet at `-72 dBm`
- **THEN** the FDN enters the player-detected state, displays `"PLAYER"` / `"NEARBY"` with brightness `80` for `2500 ms`, and returns to the idle state when the timer expires

### Requirement: Connected PDN provisioning vs. authorization routing

When a PDN connects to the FDN on a serial jack (entering the connection-detected state), the FDN SHALL verify the cabled PDN's identity and route between player provisioning and the FDN hacking/puzzle experience:

- The FDN SHALL accept the connected PDN's player identity only from the verified MAC address of the PDN physically Connected on the FDN's serial jack (ignoring wireless connection packets from any other sender MAC).
- **Unprovisioned PDN (`nil` player UUID / `0xFFFF` wire sentinel)**:
  - When an unprovisioned PDN is Connected on an FDN serial jack and a player presents their credential at the FDN, the FDN SHALL execute the player-provisioning lookup and delivery flow (`player-provisioning`): switching to Wi-Fi station mode to fetch the player's protobuf profile from `GET /device/players/...`, updating the FDN's UTC wall clock from the HTTP `Date` header (`http-date-clock-sync`), re-enabling the peer-to-peer radio, and delivering the resolved profile (or lookup failure) to the cabled PDN over the provisioning channel.
- **Provisioned PDN (non-nil player identity)**:
  - On mounting the connection-detected state for a provisioned PDN, the FDN SHALL run a `500 ms` glyph loading sequence (`100 ms` per frame) and then look up the connected PDN's player identity in non-volatile storage:
    - **Authorized (hack status `1` or `2`)**: The FDN SHALL transition to the authorized state, run a `500 ms` glyph loading sequence, display `"WELCOME"` / `"ASSET #"` / `<playerId>` with a green breathing LED animation for `5000 ms`, and then transition to the `main-menu` app. If the PDN unplugs from the serial jack before the `5000 ms` welcome timer expires, the FDN SHALL return to the idle state.
    - **Unauthorized (hack status `0` or absent)**: The FDN SHALL transition to the `symbol-match` (or `hacking`) puzzle flow (`symbol-match`) so the PDN can attempt to hack this FDN.

#### Scenario: Unprovisioned PDN connected to FDN is provisioned on credential scan
- **WHEN** an unprovisioned PDN (`0xFFFF` sentinel / nil UUID) is Connected on an FDN serial jack and the player scans a valid credential at the FDN
- **THEN** the FDN fetches the player profile over Wi-Fi, re-enables the peer-to-peer radio, and delivers the profile to the cabled PDN (`player-provisioning`)

#### Scenario: Previously hacked provisioned PDN is welcomed and routed to main-menu app
- **WHEN** a cabled provisioned PDN whose player identity has stored hack status `1` or `2` connects to the FDN
- **THEN** the FDN completes the `500 ms` connection glyph sequence, transitions to the authorized state, displays `"WELCOME"` / `"ASSET #"` / `<playerId>` for `5000 ms`, and mounts the `main-menu` app

#### Scenario: Unhacked provisioned PDN is routed to symbol-match puzzle app
- **WHEN** a cabled provisioned PDN whose player identity has no hack record on this FDN completes the `500 ms` connection-detected glyph sequence
- **THEN** the FDN transitions to the `symbol-match` puzzle app

### Requirement: Main-menu lore display and unplug routing

While mounted in the `main-menu` app:

- The FDN SHALL cycle through three 3-line inverted-text passphrase lore screens every `4000 ms` while the PDN remains connected over the serial jack:
  1. `"YOU'VE HACKED INTO"` / `"ONE OF FIVE"` / `"FIXED DATA NODES"`
  2. `"THE 4th WORD OF"` / `"THE PASSPHRASE IS:"` / `"ORIGINAL"`
  3. `"FIND SPHYNX."` / `"RECITE THE PHRASE."` / `"REAP THE REWARDS."`
- When the PDN unplugs from the serial jack, the FDN SHALL exit the `main-menu` app and return to `idle`.

#### Scenario: Main-menu cycles passphrase screens every 4 seconds until PDN unplugs
- **WHEN** an authorized PDN remains plugged into the FDN in the `main-menu` app across two `4000 ms` intervals and then unplugs
- **THEN** the FDN advances from screen 1 to screen 2 at `4000 ms`, to screen 3 at `8000 ms`, and exits `main-menu` to `idle` upon unplug

### Requirement: Hacking hint screen

When the `hacking` hint screen is mounted:

- The FDN SHALL run a yellow breathing LED animation and display `"PLEASE"` / `"DISCONNECT"`.
- When the connected PDN unplugs from the serial jack or the FDN tertiary button is pressed, the FDN SHALL exit the hint screen and return to `idle`.

#### Scenario: Unplugging PDN or pressing tertiary button exits hacking hint screen
- **WHEN** the FDN is displaying `"PLEASE"` / `"DISCONNECT"` and the PDN unplugs from the serial jack (or the tertiary button is pressed)
- **THEN** the FDN exits the hacking hint screen and returns to `idle`

### Requirement: Persistent hack storage and periodic Wi-Fi uplink for hacks, matches, and crashes

Completed FDN hacks, collected PDN match records (`match-collection`), and collected/local crash records (`crash-reporting`) SHALL be persisted in non-volatile storage and uploaded periodically to the server over Wi-Fi:

- When a player completes a hack on the FDN, the FDN SHALL persist hack status `1` (hacked locally, pending upload) under a non-volatile storage key of at most `15` ASCII characters alongside a persisted pending-upload index (so pending status-`1` records survive FDN reboots) if the player is not already recorded as `1` or `2`.
- While in the idle state with no PDN actively connected, the FDN SHALL run a `300,000 ms` (5-minute) upload check timer. When the timer expires and the FDN has at least one pending hack record (status `1`), unuploaded collected match record (`match-collection`), or unuploaded crash record (`crash-reporting`), the FDN SHALL transition to the upload state.
- On mounting the upload state, the FDN SHALL explicitly transition the wireless subsystem into Wi-Fi HTTP station mode (disabling the peer-to-peer radio and initiating Wi-Fi station association with the configured access point), run a `500 ms` glyph loading sequence followed by displaying `"SYNCING"` with a green chase LED animation, and start a `15,000 ms` fallback timeout timer.
- While Wi-Fi HTTP mode is active, background peer-to-peer send attempts SHALL be rejected without automatically switching the wireless subsystem out of Wi-Fi mode or aborting in-flight HTTP requests.
- Once queued for the upload cycle:
  - Pending collected match records SHALL be uploaded in protobuf batches via `POST /quickdraw-matches` (`match-collection`).
  - Pending local and relayed PDN crash records SHALL be uploaded via `POST /device-logs` (`crash-reporting`).
  - Pending hack records (status `1`) SHALL be uploaded via authenticated HTTP `POST` requests to `/api/boxes` with a properly escaped JSON body `{"playerId":"<id>","boxId":<boxId>,"hacked":true}`; upon each successful response, the FDN SHALL update that player's persisted hack status from `1` to `2` and remove the player from the persisted pending-upload index.
- The FDN SHALL distinguish a completed upload cycle from an unreachable Wi-Fi/server uplink: when all queued upload requests complete within the window, the FDN SHALL re-enable the peer-to-peer radio and return to the idle state; if the `15,000 ms` fallback timeout expires or the HTTP requests fail before all pending records are confirmed, the FDN SHALL cancel any remaining in-flight HTTP requests, surface an upload failure indication, retain unconfirmed records in non-volatile storage for the next 5-minute cycle, re-enable the peer-to-peer radio, and return to the idle state.

#### Scenario: Five-minute idle timer uploads pending records and restores peer-to-peer radio
- **WHEN** the `300,000 ms` idle upload timer expires on an FDN with box ID `7`, pending player hack `"0042"` (status `1`), and collected PDN match records, and the server responds with HTTP 2xx
- **THEN** the FDN transitions to Wi-Fi HTTP mode, uploads the pending match batch and posts `{"playerId":"0042","boxId":7,"hacked":true}` to `/api/boxes`, updates player `"0042"` to status `2` in non-volatile storage, re-enables the peer-to-peer radio, and returns to the idle state

#### Scenario: Upload fallback timeout cancels in-flight HTTP, preserves pending records, and restores peer-to-peer radio
- **WHEN** the upload state is mounted with pending records and Wi-Fi or the server does not respond within `15,000 ms`
- **THEN** the `15,000 ms` fallback timer expires, any in-flight HTTP requests are cancelled, an upload failure indication is surfaced, unconfirmed records remain persisted for the next 5-minute cycle, the peer-to-peer radio is re-enabled, and the FDN returns to the idle state
