## MODIFIED Requirements

### Requirement: Hub awaken sequence and idle screen behavior

The hub app SHALL enter via the awaken sequence before mounting the idle state:

- On entry to the awaken sequence, the PDN SHALL display the player's allegiance artwork, start the haptic motor at medium intensity (`125`), and step the haptic motor every `100 ms` for `20` steps (`2000 ms` total, alternating full intensity `255` and off `0` on each step) before transitioning to the idle state and turning the haptic motor off.
- On mounting the idle state, the PDN SHALL ensure the peer-to-peer radio is enabled, start the role-appropriate (`Hunter` or `Bounty`) idle LED animation, and display screen `0`, the player's name. Every time the idle state is mounted it SHALL start on screen `0`. On screens `1` through `8` the PDN SHALL display the player's allegiance artwork alongside the current statistics screen.
- While in the idle state, clicking either the primary button or the secondary button SHALL cycle forward through nine screens (`0` through `8`, wrapping from `8` back to `0`):
  - `0`: The player's name (`"Player"`)
  - `1`: Total wins (`"Wins"`)
  - `2`: Current win streak (`"Streak"`)
  - `3`: Total losses (`"Losses"`)
  - `4`: Total matches played (`"Matches"`)
  - `5`: Last reaction time in milliseconds (`"Last"` / `"Reaction"`)
  - `6`: Average reaction time in milliseconds (`"Average"` / `"Reaction"`)
  - `7`: Current posse supporter count (`"Posse"`)
  - `8`: Player symbol glyph
- Screen `0` SHALL show the label `"Player"` in the small font and the player's display name (up to `31` characters) in the large font, using the full width of the display, on up to `3` lines. The name SHALL be broken at spaces. If text is left over after `3` lines, the whole name SHALL be broken again by filling each line as far as it goes, and only text that still does not fit `3` lines SHALL be cut. The font is proportional, so a `31`-character name with many wide letters can still be cut. A PDN on the demo profile SHALL show the demo profile's name, `Demo`.
- Screens `1`, `3`, `4` and `6` SHALL add the totals baseline the PDN received at registration (`player-provisioning`; `0` when the server sent none, and for the demo profile) to the PDN's session statistics, which count from its last restart:
  - Wins: baseline wins plus session wins.
  - Losses: baseline matches played minus baseline wins, plus session losses.
  - Matches: baseline matches played plus session matches played.
  - Average reaction: baseline average draw time times baseline matches played, plus the session's total reaction time, all divided by baseline matches played plus session matches played, rounded down; `0` when both counts are `0`. The arithmetic SHALL be 64-bit. A reaction time recorded for a match abandoned before it counted SHALL add to the session's total reaction time: with a baseline it is weighted in, and with no baseline the screen shows `0` until a match counts.
- Screens `2`, `5`, `7` and `8` SHALL show what they show without a baseline: the session's win streak, the session's last reaction time, the count of cabled posse members, and the symbol glyph.
- The PDN SHALL NOT mark a displayed total as provisional or incomplete.

#### Scenario: Clicking primary or secondary button cycles forward through 8 idle screens
- **WHEN** a PDN is mounted in the hub idle state on screen `0`, the player's name, and the user clicks the primary or secondary button nine times
- **THEN** the display advances sequentially through screens `1` through `8` on the first eight clicks, and the ninth click wraps back to screen `0`

#### Scenario: Idle starts on the player's name
- **WHEN** a PDN registered to a player named `"Ada"` mounts the hub idle state, at first entry or on returning from a match
- **THEN** the display shows the label `"Player"` and the name `"Ada"`

#### Scenario: Long name is shown whole on up to three lines
- **WHEN** a PDN registered to a player whose display name has spaces between its words, and whose words fit `3` lines when broken at spaces, mounts the hub idle state
- **THEN** the whole name is shown in the large font on at most `3` lines, broken at spaces

#### Scenario: Name too wide for three lines is cut
- **WHEN** a PDN registered to a player whose display name is `31` letters `W` with no space mounts the hub idle state
- **THEN** the name fills `3` lines and the text that does not fit is not shown

#### Scenario: Demo profile shows its name
- **WHEN** a PDN on the demo profile mounts the hub idle state
- **THEN** the display shows the label `"Player"` and the name `"Demo"`

#### Scenario: Totals screens add the baseline to the session statistics
- **WHEN** a PDN with a baseline of `12` matches played, `7` wins and an average draw time of `300 ms` has session statistics of `3` matches played, `2` wins, `1` loss and a total reaction time of `700 ms`
- **THEN** the screens show `9` wins, `6` losses, `15` matches and an average reaction of `286 ms`

#### Scenario: Average is zero with no matches
- **WHEN** a PDN registered against a server that sent no totals has played no match since its restart
- **THEN** the average reaction screen shows `0`

#### Scenario: Reaction time from an abandoned match
- **WHEN** a PDN with a baseline of `10` matches played and an average draw time of `300 ms` records a reaction time of `410 ms` for a match that is abandoned before it counts
- **THEN** the average reaction screen shows `341 ms`, and a PDN with no baseline in the same case shows `0`

#### Scenario: No totals from the server shows the session statistics alone
- **WHEN** a PDN registered against a server that sent no totals has session statistics of `2` matches played, `1` win and `1` loss
- **THEN** the screens show `1` win, `1` loss and `2` matches

#### Scenario: Streak and last reaction come from the session only
- **WHEN** a PDN with a baseline of `7` wins mounts the hub idle state before playing any match since its restart
- **THEN** the streak screen shows `0` and the last reaction screen shows `0`

#### Scenario: A displayed total can go down at the next registration
- **WHEN** a PDN showing `15` matches, of which `3` were played this session against a teammate, is restarted and registered to the same player again, and the lookup returns `12` matches played
- **THEN** the matches screen shows `12`
