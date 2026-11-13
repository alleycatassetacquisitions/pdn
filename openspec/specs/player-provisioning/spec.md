# player-provisioning Specification

## Purpose

PDNs do not connect to Wi-Fi, do not issue HTTP requests directly, and do not support offline registration. Instead, a player registers a PDN by connecting it to an FDN (Fixed Data Node) on a serial jack and scanning their player credential at the FDN. The FDN resolves the player's profile from the central server over Wi-Fi using the protobuf device API and delivers the resolved profile to the cabled PDN over a reliable peer-to-peer provisioning channel. The `player-provisioning` capability defines the unprovisioned PDN state, FDN credential lookup, protobuf profile decoding and bounds, cabled FDN-to-PDN profile delivery, session profile application, and the PDN provisioning user experience.

This specification states the required outcomes, invariants, and environmental constraints of player provisioning.

Terms used throughout:

- **Player UUID**: the non-nil 16-byte universally unique identifier assigned to a registered player by the server.
- **Team UUID**: the 16-byte identifier of the player's assigned game role team (`Hunter` or `Bounty`), or the nil UUID (`16` zero bytes) when unassigned.
- **Affiliation**: a `(category UUID, category name, affiliation UUID, affiliation name)` tuple describing a player's lore allegiance or faction membership.
- **Provisioned profile**: the registration record delivered from an FDN to a PDN, comprising the 16-byte player UUID, display name, team UUID and team name, affiliations, and optional initial player statistics.

## Requirements

### Requirement: Unprovisioned PDN state and prohibition of offline or PDN Wi-Fi registration

An unprovisioned PDN SHALL register through a cabled FDN without using Wi-Fi or offering an offline fallback:

- On boot, a PDN SHALL initialize with no registered profile and a nil 16-byte player UUID (never generating a random player ID at boot that could masquerade as a provisioned profile). After the boot splash hold, an unprovisioned PDN SHALL show the registration screen.
- The registration screen SHALL take a 4-digit PIN on the PDN's buttons. On a PIN other than `0000`, the PDN SHALL send the PIN to the FDN Connected on one of its jacks (as soon as one is Connected) over a reliable peer-to-peer channel, resend it if the radio gives up on delivering it, wait up to `10,000 ms` from the first send for the FDN's reply (failing `10,000 ms` after PIN entry if no FDN is cabled by then), and on no reply show the lookup failure screen.
- A PDN SHALL accept a reply only if it names the PIN it sent, so a late reply to an earlier attempt is not applied.
- PIN `0000` SHALL offer a demo role picker (Hunter or Bounty) and then enter the hub app. A demo player has no server player UUID. Its connection context SHALL mark a profile as applied, so an FDN does not treat it as unprovisioned.
- A PDN SHALL NOT enable Wi-Fi station mode, SHALL NOT issue HTTP player-lookup requests directly, and SHALL NOT offer offline registration.
- While on the registration screen, the PDN SHALL remain eligible to receive peer-to-peer firmware distribution images and SHALL start seeding its running firmware image when the secondary button is released and held for `5000 ms` (`firmware-distribution`).

#### Scenario: Unprovisioned PDN waits on the registration screen without Wi-Fi
- **WHEN** a PDN boots with no provisioned profile and no FDN connected
- **THEN** the PDN has a nil player UUID, shows the registration screen with the peer-to-peer radio enabled, and never enables Wi-Fi or offers offline registration

#### Scenario: PIN entered with an FDN cabled is looked up by the FDN
- **WHEN** a player enters PIN `1234` on an unprovisioned PDN Connected to an FDN
- **THEN** the PDN sends `1234` to that FDN, which issues `GET /device/players/by-pin/1234`

#### Scenario: Reply to an earlier PIN is ignored
- **WHEN** a PDN waiting on PIN `0007` receives a profile reply naming PIN `4321`
- **THEN** the PDN does not apply the profile

#### Scenario: Demo PIN enters the hub without a server identity
- **WHEN** a player enters PIN `0000` and picks Hunter
- **THEN** the PDN enters the hub app as a Hunter with a nil player UUID

#### Scenario: Holding secondary button for 5 seconds on registration screen starts firmware seeding
- **WHEN** a user releases and holds the secondary button for `5000 ms` on the PDN registration screen
- **THEN** the PDN enters the firmware distribution seeding state (`firmware-distribution`)

### Requirement: FDN server lookup and protobuf profile decoding

When a player's credential is presented at an FDN while a PDN is Connected on a serial jack, the FDN SHALL validate the credential and look up the player's registration from the server:

- The FDN SHALL validate that the scanned credential consists only of permitted URL-safe credential characters (such as a 4-digit ASCII PIN `[0-9]{4}`) before issuing an authenticated HTTPS `GET` request (`Authorization: Bearer <token>`) to the server's device-player lookup endpoint.
- On a successful HTTP `200 OK` response (with a bounded response body size so oversized payloads are rejected before exhausting memory), the FDN SHALL decode the protobuf payload via the device-player codec:
  - The player UUID, category UUIDs, and affiliation UUIDs SHALL each be validated as exactly `16` raw bytes and non-nil (not `16` zero bytes); a missing, non-16-byte, or all-zero required UUID SHALL cause the response to be rejected as undecodable.
  - If a team is present, its team UUID SHALL be validated as `16` raw bytes and non-nil; if absent, the team UUID SHALL be recorded as the nil UUID (`16` zero bytes).
  - Display names (player name, team name, category name, and affiliation name) SHALL be stored as null-terminated ASCII strings truncated to at most `31` characters (`32`-byte buffer, from server strings up to `255` bytes); an absent name SHALL be stored as the empty string.
  - Up to `5` affiliations SHALL be retained in server response order; any additional affiliations beyond the first `5` SHALL be skipped without failing the decode.
  - If initial player statistics (career wins, losses, matches played, win streak, or reaction-time history) are provided, they SHALL be retained in the decoded profile; otherwise, session statistics SHALL initialize to `0`.
- If the HTTP response body cannot be decoded as a valid player profile, or if the HTTP layer reports a transport or non-2xx error, the FDN SHALL report a lookup failure to the cabled PDN rather than delivering a partial profile.

#### Scenario: FDN decodes player profile with team, affiliations, and truncated display name
- **WHEN** the server responds with a valid non-nil 16-byte player UUID, a 45-byte ASCII display name, a valid team, and 6 affiliations
- **THEN** the codec retains the 16-byte player UUID, truncates the display name to 31 ASCII characters (`32`-byte null-terminated buffer), retains the team UUID and name, and retains the first 5 affiliations in server order

#### Scenario: Missing, wrong-length, or nil required UUID in server response is rejected
- **WHEN** the server responds with a player or affiliation UUID whose length is not 16 bytes or whose 16 bytes are all zero
- **THEN** the codec rejects the response as undecodable and no profile is delivered to the PDN

### Requirement: Cabled FDN-to-PDN profile delivery and wire identity

Once the FDN decodes a valid player profile (or a lookup failure) for a Connected PDN, the FDN SHALL deliver the result to that cabled PDN over a reliable peer-to-peer provisioning channel:

- An unprovisioned PDN whose session has no registered profile SHALL advertise the unregistered sentinel identity (nil 16-byte UUID, mapping to `0xFFFF` on any 16-bit wire field and never `0`) in its outbound connection context until a profile is applied.
- The PDN SHALL accept a provisioned profile or lookup-failure notification **only while mounted in the provisioning app** and **only when the wireless sender MAC address matches the verified MAC address of an FDN currently Connected on a physical serial jack** (ignoring wireless provisioning frames from any uncabled sender or after the PDN has entered gameplay).
- Once provisioned, the PDN's connection context and peer profile exchanged with cabled neighbors SHALL carry the player's 16-byte player UUID, 16-byte team UUID, standing role (`Hunter` or `Bounty`), and display name (truncated to fit the wire profile field).

#### Scenario: Unprovisioned PDN advertises unregistered sentinel before profile delivery
- **WHEN** an unprovisioned PDN connects to a peer on a serial jack before receiving a player profile
- **THEN** the PDN's outbound connection context carries the unregistered sentinel identity (nil UUID / `0xFFFF`, never `0`)

#### Scenario: Uncabled wireless provisioning frame is rejected by PDN
- **WHEN** an unprovisioned PDN receives a wireless profile-delivery or lookup-failure frame from a sender MAC that is not Connected as an FDN on a physical serial jack
- **THEN** the PDN ignores the frame and remains on the provisioning screen

### Requirement: Session profile application, role/allegiance mapping, and welcome flow

When an unprovisioned PDN in the provisioning app receives a resolved player profile from its cabled FDN:

- The PDN SHALL store the profile on the single session player instance:
  - A team name of `"Hunter"` SHALL set the player's standing role to `Hunter`; `"Bounty"`, an unassigned team (nil team UUID), or any unrecognized team name SHALL set the standing role to `Bounty`.
  - From the player's affiliations, an affiliation matching `"Helix*"` SHALL map to Helix, `"Endline*"` to Endline, `"Reboot*"` or `"Resistance*"` to Resistance, and `"Alleycat*"` (or any unrecognized/absent affiliation) to Alleycat.
  - Applying the profile while any serial jack is Connected SHALL trigger a live context update to connected neighbors so peers learn the PDN's newly registered standing role and identity.
- After applying the profile, the PDN SHALL transition to the welcome screen displaying `"Welcome"` at `y = 24` and the player's display name at `y = 44` for `5000 ms`, and then transition into the hub app (`pdn-session-routing`), which runs the 20-step (`100 ms` per step) haptic awaken sequence before mounting the hub idle state.
- If the cabled FDN reports that the player lookup failed or timed out, the PDN SHALL display `"Unable to"` / `"Locate Asset"` for `3000 ms` and return to the provisioning screen.

#### Scenario: Applied profile maps role and allegiance and transitions through welcome to hub app
- **WHEN** an unprovisioned PDN receives a profile from its Connected FDN with display name `"Ada"`, team `"Hunter"`, and affiliation `"Helix"`
- **THEN** the session player's standing role is set to `Hunter`, allegiance is set to Helix, the PDN displays `"Welcome"` and `"Ada"` for `5000 ms`, and transitions to the hub app (`Awaken` -> `Hub Idle`)

#### Scenario: Lookup failure from cabled FDN displays asset error for 3000 ms and returns to provisioning screen
- **WHEN** a player lookup at the Connected FDN fails or times out while the PDN is waiting in the provisioning app
- **THEN** the PDN displays `"Unable to"` / `"Locate Asset"` for `3000 ms` and returns to the provisioning screen
