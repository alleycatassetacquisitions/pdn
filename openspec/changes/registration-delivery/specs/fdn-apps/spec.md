## MODIFIED Requirements

### Requirement: Connected PDN provisioning vs. symbol-match routing

When a PDN connects to the FDN on an upstream serial jack, the FDN SHALL inspect the cabled PDN's connection context from the verified MAC address of that jack and route between player provisioning and the symbol-match hacking game:

- **Unprovisioned PDN (its connection context reports no applied profile)**:
  - While an unprovisioned PDN is Connected on an FDN serial jack, the FDN SHALL show the registration screen (Registration screen for band taps). When a band is tapped at the FDN's reader while that PDN's jack holds the registration turn (`player-provisioning`), the FDN SHALL execute the player-provisioning lookup and delivery flow (`player-provisioning`): switching to Wi-Fi station mode to fetch the player's protobuf profile from `GET /device/players/by-band/{uid}` (`device-player-lookup`) from a server that presents the pinned key (`fdn-server-trust`), updating the FDN's UTC wall clock from the HTTP `Date` header (`http-date-clock-sync`), re-enabling the peer-to-peer radio, and delivering the record key over that PDN's jack and then the resolved profile, or the lookup failure, to the cabled PDN.
- **Provisioned PDN (its connection context reports an applied profile, the demo profile included)**:
  - The FDN boots into the `symbol-match` app (`symbol-match`) and stays in it, so connected provisioned players can play the dual-jack symbol-matching hacking puzzle and reveal the FDN's codeword; the registration screen gives way to it once no PDN waits to register, no lookup runs and no result holds.
  - The registration screen SHALL NOT interrupt the 30-second codeword success hold.

#### Scenario: Unprovisioned PDN connected to FDN is provisioned on credential scan
- **WHEN** an unprovisioned PDN (its context reports no applied profile) is Connected on an FDN serial jack that holds the registration turn and the player taps a band linked to them at the FDN
- **THEN** the FDN fetches the player profile over Wi-Fi, re-enables the peer-to-peer radio, writes the record key on that jack and then delivers the profile to the cabled PDN (`player-provisioning`)

#### Scenario: Provisioned PDN connected to FDN routes to symbol-match app
- **WHEN** a provisioned PDN connects to an FDN serial jack
- **THEN** symbol match runs on the FDN for puzzle play

## ADDED Requirements

### Requirement: Registration screen for band taps

While a PDN waits to register, a lookup runs or its result holds, the FDN SHALL show the registration screen in place of symbol match, except during the codeword hold. It SHALL show `"TAP BAND"` over the label of the jack holding the turn (`"JACK 1"` primary, `"JACK 2"` secondary), `"NO READER"` over it while the band reader does not answer, and `"LOOKING"` / `"UP"` during a lookup. No screen SHALL show while the head's MAC is the peer of both jacks. No PIN is asked for.

#### Scenario: Turn's jack is named
- **WHEN** an unprovisioned PDN becomes Connected on the secondary jack of an FDN with no other PDN
- **THEN** the FDN displays `"TAP BAND"` / `"JACK 2"`

#### Scenario: Missing reader is shown
- **WHEN** an unprovisioned PDN is Connected on the primary jack and the band reader does not answer
- **THEN** the FDN displays `"NO READER"` / `"JACK 1"` and registers nobody

#### Scenario: Screen gives way once nobody waits
- **WHEN** the last unprovisioned PDN leaves and no lookup runs or result holds
- **THEN** the FDN returns to symbol match

### Requirement: Registration result screens

A lookup's result SHALL stay on the registration screen for `3000 ms`: `"WELCOME"` over the player's name once key and profile went to the PDN; `"UNABLE TO"` / `"ISSUE KEY"` for a profile with no usable key; `"SEE DESK"` over the jack label for no linked player; `"SERVER"` / `"NOT TRUSTED"` for a server without the pinned key; `"PDN LEFT"` over the jack label when the PDN left before its key and profile; otherwise `"UNABLE TO"` / `"LOCATE ASSET"`.

#### Scenario: Profile without a key
- **WHEN** the server answers a tap with a profile and no record key
- **THEN** the FDN displays `"UNABLE TO"` / `"ISSUE KEY"` for `3000 ms` and registers nobody

#### Scenario: Unlinked band sends the player to the desk
- **WHEN** the server answers `404` to a tap for the primary jack
- **THEN** the FDN displays `"SEE DESK"` / `"JACK 1"` for `3000 ms`
