## ADDED Requirements

### Requirement: Handing a PDN to another player

A PDN SHALL be handed to another player by restarting it. After a restart a PDN SHALL be on PIN entry with no registered profile and no record key (`record-authentication`), and registration SHALL be reachable only from there. A running PDN SHALL offer no sign-out and no way back to registration: a PDN that is not restarted SHALL stay registered to the player it was registered to.

#### Scenario: Restart returns the PDN to PIN entry with nothing held
- **WHEN** a PDN registered to a player is restarted
- **THEN** the PDN shows PIN entry with a nil player UUID, no record key, a totals baseline of `0` and session statistics of `0`

#### Scenario: A PDN passed on without a restart keeps playing as the previous player
- **WHEN** a PDN registered to player $P$ is handed to another person without a restart and that person plays a match that is stored
- **THEN** the match record names $P$ and carries a record tag made under $P$'s record key

#### Scenario: No path leads from play back to registration
- **WHEN** a registered PDN is in the hub app, a match, a shootout or symbol match
- **THEN** none of those apps has a transition to the registration app, so the PDN reaches PIN entry only by restarting

### Requirement: Scoring totals from the lookup

The lookup response MAY carry the looked-up player's scoring totals: matches played, wins and average draw time in milliseconds, over the matches the server scores. The FDN SHALL take the totals it finds from the same response as the profile. A response with none of them, or with only some, SHALL NOT fail the lookup: each absent total SHALL count as `0`, and the registration SHALL proceed as it does without totals.

#### Scenario: Lookup response with totals
- **WHEN** the server responds with a valid profile, a record key, and totals of `12` matches played, `7` wins and an average draw time of `300 ms`
- **THEN** the FDN holds those three totals with the decoded profile

#### Scenario: Lookup response with no totals still registers the PDN
- **WHEN** the server responds with a valid profile and a record key and no totals
- **THEN** the FDN delivers the record key and the profile as usual, with totals of `0`, and reports no lookup failure

### Requirement: Scoring totals delivered with the profile

The FDN SHALL send the scoring totals it took from the lookup to the PDN inside the profile it delivers over the peer-to-peer radio. The PDN SHALL keep them as its totals baseline when it applies that profile, and SHALL apply at most one profile between restarts. Wherever a profile is applied, wins above matches played SHALL drop all three totals to `0`. The baseline SHALL be for display only (`pdn-session-routing`), never uploaded or used in a game decision. The demo baseline SHALL be `0`.

#### Scenario: Baseline arrives with the profile
- **WHEN** an FDN that holds totals of `12` matches played, `7` wins and `300 ms` delivers the profile and the PDN applies it
- **THEN** the PDN's totals baseline is `12` matches played, `7` wins and `300 ms`

#### Scenario: Baseline is taken once and not refreshed
- **WHEN** a registered PDN with a baseline of `12` matches played plays `3` more matches and stays running
- **THEN** its baseline is still `12` matches played, and the `3` matches are in its session statistics only

#### Scenario: A match not yet uploaded is missing from the baseline
- **WHEN** a player whose most recent match has been uploaded by neither PDN registers on another PDN, and the lookup returns `12` matches played
- **THEN** that PDN's baseline is `12` matches played, and nothing on the PDN marks the totals as incomplete

#### Scenario: Wins above matches drop the totals
- **WHEN** a PDN applies a profile with totals of `4` matches played, `7` wins and `300 ms`
- **THEN** its totals baseline is `0` matches played, `0` wins and `0 ms`
