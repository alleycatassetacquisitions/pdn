## ADDED Requirements

### Requirement: Registration screen for a cabled unprovisioned PDN

When a PDN whose connection context carries a nil player UUID becomes Connected on an FDN serial jack while the FDN is in the idle or player-detected state, the FDN SHALL show a registration screen ahead of any other cabled flow:

- While waiting for a lookup request, the FDN SHALL display `"REGISTER"` / `"ENTER CODE"`.
- While a lookup for that PDN is in progress, the FDN SHALL display `"LOOKING"` / `"UP"`.
- On a delivered profile, the FDN SHALL display `"WELCOME"` and the player's display name for `3000 ms`; on a lookup failure or timeout, `"UNABLE TO"` / `"LOCATE ASSET"` for `3000 ms`.
- The FDN SHALL end a lookup that has not answered within `8000 ms` as a lookup failure.
- The FDN SHALL leave the registration screen for the idle state when the PDN is unplugged, when a result has been shown, or when the cabled PDN's context no longer carries a nil player UUID.

#### Scenario: Unprovisioned PDN plugged in shows the registration screen
- **WHEN** an unprovisioned PDN becomes Connected on an idle FDN
- **THEN** the FDN displays `"REGISTER"` / `"ENTER CODE"` instead of entering the symbol-match flow

#### Scenario: Successful lookup shows the player's name
- **WHEN** the FDN delivers a profile named `"Ada"` to the cabled PDN
- **THEN** the FDN displays `"WELCOME"` / `"Ada"` for `3000 ms` and returns to the idle state
