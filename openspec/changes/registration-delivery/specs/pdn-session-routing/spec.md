## MODIFIED Requirements

### Requirement: Hub awaken sequence and idle screen behavior

The hub app SHALL enter via the awaken sequence before mounting the idle state:

- On entry to the awaken sequence, the PDN SHALL display the player's allegiance artwork, start the haptic motor at medium intensity (`125`), and step the haptic motor every `100 ms` for `20` steps (`2000 ms` total, alternating full intensity `255` and off `0` on each step) before transitioning to the idle state and turning the haptic motor off.
- On mounting the idle state, the PDN SHALL ensure the peer-to-peer radio is enabled, start the role-appropriate (`Hunter` or `Bounty`) idle LED animation, and display screen `0`, the player's name, or `UNREGISTERED` when the player UUID is nil (`quickdraw-match`). Every time the idle state is mounted it SHALL start on screen `0`. On screens `1` through `8` the PDN SHALL display the player's allegiance artwork alongside the current statistics screen.
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
- Screen `0` SHALL show the label `"Player"` in the small font and the player's display name (up to `31` characters) in the large font, using the full width of the display, on up to `3` lines. The name SHALL be broken at spaces. If text is left over after `3` lines, the whole name SHALL be broken again by filling each line as far as it goes, so a word wider than a line is cut at the line width, and only text that still does not fit `3` lines SHALL be cut. A PDN whose player UUID is nil, the release demo profile included, SHALL show `UNREGISTERED` over its allegiance artwork in place of the name; a test build SHALL show its test name, `Test`.

#### Scenario: Clicking primary or secondary button cycles forward through 8 idle screens
- **WHEN** a PDN is mounted in the hub idle state on screen `0`, the player's name, and the user clicks the primary or secondary button nine times
- **THEN** the display advances sequentially through screens `1` through `8` on the first eight clicks, and the ninth click wraps back to screen `0`

#### Scenario: Idle starts on the player's name
- **WHEN** a PDN registered to a player named `"Ada"` mounts the hub idle state, at first entry or on returning from a match
- **THEN** the display shows the label `"Player"` and the name `"Ada"`

#### Scenario: Long name is broken at spaces on up to three lines
- **WHEN** a PDN whose player's display name has words that fit `3` lines when broken at spaces mounts the hub idle state
- **THEN** the whole name is shown in the large font on at most `3` lines, broken at spaces

#### Scenario: Name too wide for three lines is cut
- **WHEN** a PDN whose player's display name is `31` letters `W` with no space mounts the hub idle state
- **THEN** the name fills `3` lines and the text that does not fit is not shown

#### Scenario: Nil player UUID opens on UNREGISTERED
- **WHEN** a PDN whose player UUID is nil, such as the release demo profile, mounts the hub idle state or wraps the screen cycle
- **THEN** screen `0` shows `UNREGISTERED` and not the name
