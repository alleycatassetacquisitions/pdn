## MODIFIED Requirements

### Requirement: Connected PDN provisioning vs. symbol-match routing

When a PDN connects to the FDN on an upstream serial jack, the FDN SHALL inspect the cabled PDN's connection context from the verified MAC address of that jack and route between player provisioning and the symbol-match hacking game:

- **Unprovisioned PDN (its connection context shows no applied profile)**:
  - While an unprovisioned PDN is Connected on an FDN serial jack and a band is tapped at the FDN's reader for that jack's pending registration (`player-provisioning`), the FDN SHALL execute the player-provisioning lookup and delivery flow (`player-provisioning`): switching to Wi-Fi station mode to fetch the player's protobuf profile from `GET /device/players/by-band/{uid}` (`device-player-lookup`) from a server that presents the pinned key (`fdn-server-trust`), updating the FDN's UTC wall clock from the HTTP `Date` header (`http-date-clock-sync`), re-enabling the peer-to-peer radio, and delivering the resolved profile (or lookup failure) to the cabled PDN over the provisioning channel.
- **Provisioned PDN (its connection context shows an applied profile)**:
  - When a provisioned PDN is Connected on an FDN serial jack, the FDN SHALL transition to the `symbol-match` app (`symbol-match`) so connected players can play the dual-jack symbol-matching hacking puzzle and reveal the FDN's codeword.
  - When no provisioned PDN remains Connected on either upstream serial jack (outside the 30-second codeword success hold), the FDN SHALL return to the `idle` app.

#### Scenario: Unprovisioned PDN connected to FDN is provisioned on credential scan
- **WHEN** an unprovisioned PDN (its context shows no applied profile) is Connected on an FDN serial jack whose registration is pending and the player taps a linked band at the FDN
- **THEN** the FDN fetches the player profile over Wi-Fi, re-enables the peer-to-peer radio, and delivers the profile to the cabled PDN (`player-provisioning`)

#### Scenario: Provisioned PDN connected to FDN routes to symbol-match app
- **WHEN** a provisioned PDN connects to an FDN serial jack
- **THEN** the FDN mounts the `symbol-match` app for puzzle play

## ADDED Requirements

### Requirement: Registration screens for band taps

While any registration is queued (`player-provisioning`) and symbol match is not running, the FDN SHALL show the registration screen. While one is pending it SHALL show `"TAP BAND"` over the pending jack's label in place of `"REGISTER"` / `"ENTER CODE"`: `"JACK 1"` for the primary jack, `"JACK 2"` for the secondary jack. While the tap's lookup runs it SHALL show `"LOOKING"` / `"UP"`.

#### Scenario: Pending registration names its jack
- **WHEN** an unprovisioned PDN becomes Connected on the secondary jack of an idle FDN
- **THEN** the FDN displays `"TAP BAND"` / `"JACK 2"`

#### Scenario: Next jack is named when its turn comes
- **WHEN** the primary jack's registration ends while the secondary jack's registration is queued
- **THEN** the FDN displays `"TAP BAND"` / `"JACK 2"`

#### Scenario: Symbol match keeps its screen while a PDN is queued
- **WHEN** an unprovisioned PDN becomes Connected on the secondary jack while symbol match is running with a provisioned PDN on the primary jack
- **THEN** the FDN keeps the symbol-match screen and shows no registration screen until symbol match ends

### Requirement: Registration failure screens

In place of `"UNABLE TO"` / `"LOCATE ASSET"`, each for `3000 ms`: when the server answers `404` (no player matches the band) or `400` (malformed UID), the FDN SHALL show `"SEE DESK"` over the jack label; when no answer comes within `8000 ms` or the request fails in transport, `"SERVER"` / `"UNREACHABLE"`; when the server does not present the pinned key (`fdn-server-trust`), `"SERVER"` / `"NOT TRUSTED"`. Any other lookup failure SHALL keep `"UNABLE TO"` / `"LOCATE ASSET"`.

#### Scenario: Unknown or unlinked band sends the player to the desk
- **WHEN** the server answers `404` to the lookup of a band tapped for the primary jack
- **THEN** the FDN displays `"SEE DESK"` / `"JACK 1"` for `3000 ms`

#### Scenario: Lookup with no answer shows the server is unreachable
- **WHEN** a lookup has no answer within `8000 ms`
- **THEN** the FDN displays `"SERVER"` / `"UNREACHABLE"` for `3000 ms`

#### Scenario: Server without the pinned key is not trusted
- **WHEN** the server of a lookup presents a key whose hash is not the FDN's pin
- **THEN** the FDN displays `"SERVER"` / `"NOT TRUSTED"` for `3000 ms`

### Requirement: Registration screen with no reader

While the band reader does not respond (`fdn-band-reader`), the FDN SHALL show `"NO READER"` over the pending jack's label in place of `"TAP BAND"`. A pending registration SHALL still time out and requeue (`player-provisioning`). Such an FDN registers nobody, staff cards included.

#### Scenario: Missing reader is shown
- **WHEN** a registration is pending on the primary jack and the band reader does not respond
- **THEN** the FDN displays `"NO READER"` / `"JACK 1"`
