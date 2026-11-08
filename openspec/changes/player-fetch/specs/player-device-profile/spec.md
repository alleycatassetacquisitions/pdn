## ADDED Requirements

### Requirement: Session retains full player profile received from Connected FDN

After a successful player registration via a Connected FDN (`player-provisioning`), the PDN session SHALL retain the decoded player profile (non-nil 16-byte player UUID, 16-byte team UUID or nil UUID, display name up to `31` ASCII characters in a `32`-byte buffer, and up to `5` affiliations) for welcome display and gameplay.

#### Scenario: Successful FDN registration populates session profile
- **WHEN** the PDN receives a valid player profile with display name `"Ada"`, team, and affiliation fields from its Connected FDN
- **THEN** the session retains that profile and the welcome screen displays `"Ada"`

### Requirement: Player profile populates standing role and allegiance with safe defaults

When a registration profile from a Connected FDN is applied to the session player, the PDN SHALL derive the player's standing role (`Hunter` for team `"Hunter"`, or `Bounty` for `"Bounty"`, unassigned, or unrecognized team) and faction allegiance (`Helix`, `Endline`, `Resistance` for `"Reboot*"` or `"Resistance*"`, or `Alleycat` for `"Alleycat*"` or any unrecognized/absent affiliation).

#### Scenario: Registered profile sets standing role and allegiance
- **WHEN** a player profile with team `"Hunter"` and affiliation `"Helix"` is applied from a Connected FDN
- **THEN** the player's standing role is `Hunter` and the player's allegiance is `Helix`
