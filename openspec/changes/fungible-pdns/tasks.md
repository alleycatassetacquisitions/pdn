## 1. Decisions

- [x] 1.1 Owner ruled: a PDN is handed over by restarting it; no sign-out is added; the idle screen shows the registered player's name; a PDN passed on without a restart stays the previous player's, accepted; clearing leaves the legacy fields set, as it does today
- [x] 1.2 Owner ruled: the PDN shows the player's server totals after registering; the server item is last in the ranking, a feature and not a security item, and does not hold up the roll-out gate
- [x] 1.3 Owner ruled: a posse supporter counts only if its PDN is registered, to a different player than the challenger; a second PDN registered as another player still passes
- [ ] 1.4 Owner confirms the editor's choices F1 to F3, T1 to T8, P1, P2 and P4 (design.md, Open Questions)
- [ ] 1.5 Owner answers: do two supporters registered as the same player both count; who is told the handover rule; how a PDN's power is cycled at the event

## 2. Name on idle and the handover rule

- [ ] 2.1 The idle screen list has nine screens; the name screen is index `0`; the eight existing screens move to `1` through `8`; idle starts on screen `0` every time it is mounted
- [ ] 2.2 Name screen: the label `"Player"` in the small font and the display name in the large font over the full width, with no allegiance artwork
- [ ] 2.3 Break the name over up to `3` lines using the display's measured text width: at spaces first; if text is left over, break the whole name again by filling each line; cut only the text that still does not fit three lines
- [ ] 2.4 A PDN on the demo profile shows `Demo` on the name screen
- [ ] 2.5 Clearing the session player provider also resets the totals baseline (3.4); it keeps leaving the legacy fields (name, id, role, allegiance, faction, symbol) and the session statistics
- [ ] 2.6 The simulator's `reboot` / `restart` command is a restart: the device gets a fresh player and player provider (no statistics, no baseline, no PIN, no record key) and lands on PIN entry
- [ ] 2.7 Tests: nine clicks from screen `0` return to screen `0`; idle shows the name on first entry and on returning from a match; a name whose words fit three lines is broken at spaces; a `31`-character name with no space fills the lines; a name too wide for three lines shows three full lines and is cut; the demo profile shows `Demo`; clearing leaves the role and the session statistics and drops the PIN, the record key and the baseline; the simulator's restart lands on PIN entry with nothing held
- [ ] 2.8 Tests for the handover rule: a registered PDN that stores a match names its registered player and tags the record under that player's key; no app transition under `src/pdn` targets the registration app

## 3. Server totals

- [ ] 3.1 Add the placeholder fields `optional uint32 scoring_matches_played = 11`, `optional uint32 scoring_wins = 12` and `optional uint32 scoring_average_draw_time_ms = 13` to the firmware's copy of `DevicePlayerResponse`, marked `SERVER-SEAM(fungible-pdns)`, and regenerate the nanopb outputs
- [ ] 3.2 The lookup decode holds each total that is present on the `Player` and `0` for each that is absent; absent totals never fail the decode
- [ ] 3.3 The profile packet the FDN sends the PDN gains the three totals as fixed-width fields; the encode and decode on both sides copy them; the test that checks the packet's length follows
- [ ] 3.4 The totals live on the `Player` on both devices. `Player::applyRegistrationProfile` keeps them as the baseline and drops all three to `0` when wins exceed matches played; the baseline does not change again in that session; the demo profile holds `0`
- [ ] 3.5 Idle screens: wins, losses, matches and average reaction show baseline plus session as design.md T5 states; the average is computed in 64-bit arithmetic, rounded down, and `0` when both counts are `0`; streak and last reaction stay session only
- [ ] 3.6 The simulator's lookup response encoder and the test encoders can carry the three totals
- [ ] 3.7 Tests: a response with all three totals; with none; with only some; wins above matches drops the totals; totals reach the PDN in the profile; the baseline is unchanged after matches are played; the four sums, with the example in `pdn-session-routing` (`9` wins, `6` losses, `15` matches, `286 ms`); both counts `0` shows an average of `0`; a reaction time from an abandoned match is weighted into the average with a baseline and shows `0` without one; totals near the `32`-bit limit do not wrap the average; no totals shows the session counters alone; streak and last reaction ignore the baseline
- [ ] 3.8 With 3.1, extend the `player-types` delta that `pdn-record-auth-and-relay` 10.8 adds, so the clause "without changing protobuf definitions" also allows the three totals fields

## 4. Posse supporter eligibility

- [ ] 4.1 The posse confirmation carries the supporter's own 16-byte player UUID, all-zero for a PDN that is not registered and for a PDN on the demo profile; the payload is `23` bytes
- [ ] 4.2 The challenger counts a confirmation toward the boost only when its player UUID is not all-zero and differs from the challenger's own player UUID, and logs each confirmation that fails the test
- [ ] 4.3 The posse member list, the cascade, the events sent to supporters, the supporter's screens and the idle "Posse" count are unchanged
- [ ] 4.4 The simulator gives each device a different player; a simulated PDN started straight into play has no registration and does not count as a supporter
- [ ] 4.5 Tests: two supporters registered to other players give `30 ms`; a supporter registered as the challenger's own player gives nothing while another still gives `15 ms`; a supporter on the demo profile gives nothing; a demo challenger is boosted by a registered supporter; a supporter that does not count stays in the member list and is sent the posse result; the `270 ms` cap still holds

## 5. Server (outside this repository)

- [ ] 5.1 Record the server team's answer on the totals item (item 5 of the ranked server list in `pdn-record-auth-and-relay`): the carrier (fields on the lookup response or a companion call), the names and numbers, which matches the totals count, and whether they add a win streak
- [ ] 5.2 When the server team has answered, move the placeholder fields (3.1) to the agreed names and numbers; or add the companion call and change the two requirements that take the totals from the lookup response (design.md, Migration Plan step 3)

## 6. Verification on devices

- [ ] 6.1 Register a PDN; the idle state shows the player's name first, and again after a match
- [ ] 6.2 Restart a registered PDN; it shows PIN entry and its statistics screens are at `0` after the next registration against a server that sends no totals
- [ ] 6.3 Against a test server that returns totals: register, check the four totals screens show the server's numbers, play a match, check they move by one match
- [ ] 6.4 Against a server that returns no totals: registration succeeds and the totals screens show the session counters alone
- [ ] 6.5 Four PDNs: a challenger, its two supporters and an opponent, one supporter registered as the challenger's own player: the challenger's result screen shows a `15 ms` boost
- [ ] 6.6 A supporter on the demo profile behind a registered challenger, with an opponent: the result screen shows no boost

## 7. Roll-out, operations and archive

- [ ] 7.1 Tell whoever hands a PDN on the handover rule: restart it first, and check the name on its idle screen (design.md, Migration Plan step 4)
- [ ] 7.2 Every firmware PR description of this change states that the stack must not be flashed to event devices before the server verifies tags
- [ ] 7.3 At archive, rename the `pdn-session-routing` scenario "Clicking primary or secondary button cycles forward through 8 idle screens" to say nine screens; a MODIFIED requirement keeps its base scenario titles, so the delta cannot
