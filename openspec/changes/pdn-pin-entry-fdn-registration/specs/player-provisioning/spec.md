## MODIFIED Requirements

### Requirement: Unprovisioned PDN state and prohibition of offline or PDN Wi-Fi registration

An unprovisioned PDN SHALL register through a cabled FDN without using Wi-Fi or offering an offline fallback:

- On boot, a PDN SHALL initialize with no registered profile and a nil 16-byte player UUID (never generating a random player ID at boot that could masquerade as a provisioned profile). After the boot splash hold, an unprovisioned PDN SHALL show the registration screen.
- The registration screen SHALL take a 4-digit PIN on the PDN's buttons. On a PIN other than `0000`, the PDN SHALL send the PIN to the FDN Connected on one of its jacks (as soon as one is Connected) over a reliable peer-to-peer channel, wait up to `10,000 ms` for the FDN's reply, and on no reply show the lookup failure screen.
- A PDN SHALL accept a reply only if it names the PIN it sent, so a late reply to an earlier attempt is not applied.
- PIN `0000` SHALL offer a demo role picker (Hunter or Bounty) and then enter the hub app. A demo player has no server player UUID; matches it plays SHALL NOT be stored for collection.
- A PDN SHALL NOT enable Wi-Fi station mode, SHALL NOT issue HTTP player-lookup requests directly, and SHALL NOT offer offline registration.
- While on the registration screen, the PDN SHALL remain eligible to receive peer-to-peer firmware distribution images and SHALL start seeding its running firmware image when the secondary button is released and held for `5000 ms` (`firmware-distribution`).

#### Scenario: PIN entered with an FDN cabled is looked up by the FDN
- **WHEN** a player enters PIN `1234` on an unprovisioned PDN Connected to an FDN
- **THEN** the PDN sends `1234` to that FDN, which issues `GET /device/players/by-pin/1234`

#### Scenario: Reply to an earlier PIN is ignored
- **WHEN** a PDN waiting on PIN `0007` receives a profile reply naming PIN `4321`
- **THEN** the PDN does not apply the profile

#### Scenario: Demo PIN enters the hub without a server identity
- **WHEN** a player enters PIN `0000` and picks Hunter
- **THEN** the PDN enters the hub app as a Hunter with a nil player UUID, and matches it plays are not stored

#### Scenario: Holding secondary button for 5 seconds on registration screen starts firmware seeding
- **WHEN** a user releases and holds the secondary button for `5000 ms` on the PDN registration screen
- **THEN** the PDN enters the firmware distribution seeding state (`firmware-distribution`)
