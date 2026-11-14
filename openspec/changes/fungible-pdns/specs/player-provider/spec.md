## MODIFIED Requirements

### Requirement: Clear registration session
Clearing the session player provider SHALL drop the registration profile on the owned player and reset the server player UUID to the unprovisioned nil UUID state. Clearing SHALL drop exactly these: the server player UUID, the server player name, the team, the affiliations, the mark that a profile is held, the registration PIN, the record key (`record-authentication`) and the totals baseline (`player-provisioning`). Clearing SHALL leave the player's legacy fields (name, id, role, allegiance, faction and symbol, which are copied from a profile when one is applied) and its session statistics as they are. This is deliberate: clearing runs only when the PIN entry screen is shown, which happens before any profile has been applied since the restart, so those fields hold nothing from an applied profile.

#### Scenario: Clear
- **WHEN** the session player provider is cleared after a profile was applied
- **THEN** the provider reports that no registered player profile is present
- **AND** the player's registration profile is cleared

#### Scenario: Clear drops the PIN, the record key and the totals baseline
- **WHEN** the session player provider is cleared while it holds a registration PIN, a record key and a totals baseline
- **THEN** it holds no registration PIN and no record key, and its totals baseline is `0`

#### Scenario: Clear leaves the legacy fields and the session statistics
- **WHEN** the session player provider is cleared on a player whose role is `Hunter` and whose session statistics show `3` matches played
- **THEN** the player's role is still `Hunter` and its session statistics still show `3` matches played
