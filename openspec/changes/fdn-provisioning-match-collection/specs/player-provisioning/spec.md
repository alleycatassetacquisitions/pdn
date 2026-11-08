## ADDED Requirements

### Requirement: PDN asks its cabled FDN to look up the entered PIN

When fetch user mounts, a PDN SHALL send the entered PIN to the FDN Connected
on one of its jacks. If no FDN is Connected, it SHALL send the PIN once one is.
It SHALL send at most one request per fetch user mount.

#### Scenario: FDN plugged in after the PIN was entered
- **WHEN** a player enters a PIN with no FDN cabled and then plugs one in
- **THEN** the PDN sends the PIN to that FDN once

### Requirement: FDN looks the PIN up and replies to the requesting PDN

An FDN SHALL accept a lookup request only from the MAC of a PDN Connected on
one of its jacks, and only when the PIN is four ASCII digits. It SHALL ignore a
request while a lookup is already in progress. It SHALL look the player up with
`device-player-lookup` over Wi-Fi, re-enable the peer-to-peer radio, and, if the
requesting PDN is still Connected, reply with either the player's profile (server
player ID, name, team, and affiliations) and the FDN's UTC wall clock, or a lookup
failure.

#### Scenario: Unknown PIN
- **WHEN** the server answers the lookup with 404
- **THEN** the FDN replies to the PDN with a lookup failure

#### Scenario: Malformed PIN
- **WHEN** the FDN receives a request whose PIN contains a non-digit
- **THEN** it does not issue an HTTP request

### Requirement: PDN applies only a well-formed reply from its cabled FDN

A PDN SHALL accept a provisioning reply only from the MAC of an FDN Connected
on one of its jacks, and only at the reply's exact size. A profile reply with a
nil player ID or an empty name SHALL be ignored. A valid profile SHALL become the
player's registration profile and move fetch user to welcome. A lookup failure
SHALL show the failure page, then return to PIN entry after 3000 ms.

#### Scenario: Reply from an uncabled device
- **WHEN** a PDN in fetch user receives a profile reply from a MAC that is not its cabled FDN
- **THEN** the PDN does not apply the profile
