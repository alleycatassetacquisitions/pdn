## Context

The owner's fact behind this change: PDNs are fungible. A player often swaps to a different PDN during the event when a battery dies or a PDN crashes, and the PDN left behind may then be used by someone else.

The owner made three rulings on it: restart to hand over, server totals on the statistics screens, and who counts as a posse supporter. Each is stated once, under Decisions.

What the code does today, with all of the firmware of `pdn-record-auth-and-relay` applied:

- **Registration is reachable only at boot.** The registration app is loaded once, as the app the PDN boots into (`src/pdn/main.cpp:139-141`). No app transition under `src/pdn` leads back to it, and the registration is cleared in one place, when the PIN entry screen is shown (`src/pdn/apps/player-fetch/pin-entry-state.cpp:17`).
- **Clearing** drops the server player UUID, the server player name, the team, the affiliations and the mark that a profile is held (`lib/core/src/game/player.cpp:250-256`), then the PIN and the record key (`lib/core/src/game/player-provider.cpp:16-20`). It leaves the legacy fields: name, id, role, allegiance, faction and symbol, which the game reads and which are copied from a profile when one is applied (`lib/core/include/game/player.hpp:131`). It also leaves the session statistics.
- **No restart control exists** on the PDN. It restarts when its power is cycled, when it crashes, or after it accepts a firmware image, which it can do only on PIN entry.
- **The idle state has eight screens** (`src/pdn/game/quickdraw-states/idle-state.cpp:104-136`): wins, streak, losses, matches, last reaction, average reaction, posse count and the symbol glyph. Each draws the allegiance artwork on the left and one value in a column on the right that is about 8 small characters wide (inferred from the layouts, not measured). None shows the player's name. After the welcome screen the player's own name is not shown on its PDN outside a shootout. A click only moves to the next screen, and the screen index goes back to the first only when idle is left (`idle-state.cpp:46-52`, `:92`).
- **The display** is `128` by `64` pixels. The large font is proportional: most letters are `11 px` wide and `M`, `W`, `m` and `w` are `15 px`, so a line holds about 11 ordinary letters or 8 wide ones. These widths are computed from the font table, not rendered. The display interface can measure a string's width and has nothing for wrapping or clipping.
- **Statistics are per-boot counters** in memory on the player the PDN is playing as (wins, losses, win streak, matches played, last reaction time, total reaction time). Nothing stores them and only a restart resets them. The total reaction time grows at the button press (`src/pdn/game/match-manager.cpp:464`); the match is counted later, in the match result state (`src/pdn/game/quickdraw-states/match-result-state.cpp:18`), so a match abandoned after a press adds a reaction time and no match.
- **The profile an FDN sends a PDN** is one fixed-layout radio packet of `584` bytes, in a frame that allows `1469` (`lib/core/include/game/player-types.hpp:59-69`). The serial jack's application frame is exactly the `32`-byte record key and has no room for anything else.
- **The lookup response** in the firmware's copy has fields `1`, `4`, `6`, `7`, `9` and the provisional `record_key = 10`; the next free number is `11`.
- **Posse membership is cable topology and MAC addresses only.** The challenger's member list is its cabled same-role neighbour plus every PDN that sent it a posse-join. A supporter counts when its confirmation was heard this round and its MAC address is in the member list. Nothing checks whether a supporter is registered or whose player it is.
- **The posse confirmation** is a `7`-byte frame, the supporter's MAC address and a sequence byte (`lib/core/include/device/drivers/peer-comms-types.hpp:118-122`), sent straight from each supporter to the challenger.
- **The connection context** between cabled PDNs carries a `29`-byte player profile with no player UUID (`lib/core/include/device/drivers/peer-comms-types.hpp:80-88`), and it reaches only the cabled neighbour.

What the server does today (alleycat-rs at `af107bf`; outside this repository):

- One endpoint computes per-player numbers, `GET /score/players`: for each player of the active event, over matches saved with `scoring_status = 'scoring'`, a score of one point per win, matches played, and the average of the player's own draw times (`src/routes/score.rs:64-87`). It is a paginated ranking with no per-player filter.
- No streak, best draw time or last draw time is computed anywhere on the server.
- Matches between teammates, with an unassigned player, outside a scoring period, or uploaded late into a closed period while late scores are switched off are stored and do not count toward any total (`src/routes/quickdraw.rs:244-255`).
- A match is visible to the totals once either of its two PDNs has uploaded it. Nothing is cached.
- The device lookup returns no number for a player.

## Goals / Non-Goals

**Goals:**

- Whoever picks up a PDN can see whose it is, and there is one stated way to make it someone else's.
- A player who swaps PDNs does not see their wins, losses, matches and average reset to zero, once the server returns totals.
- A registration against a server that returns no totals works exactly as it does today.
- A posse supporter that is not registered, or is the challenger's own player on a second PDN, adds no boost.

**Non-Goals:**

- A sign-out, or any way back to registration without a restart.
- A restart control on the PDN.
- Forcing a restart at handover.
- Totals that match the server at every moment, or a display that says when they do not.
- A streak, a best time or a last time from the server.
- Proving a supporter's identity. The player UUID in a confirmation is what the supporter's PDN says it is.
- Changing posse membership, the cascade, the `15 ms` per supporter or the `270 ms` cap.
- Fixing the differences listed under "Found, not changed".

## Decisions

### Handover: restart, and the name on the idle screen

- **The rule (owner).** A PDN is handed to another player by restarting it. A restart returns it to PIN entry with no registered profile and no record key. There is no sign-out and none is added. Registration is reachable only at boot, which is how the code already works. The idle screen shows the registered player's name so a new holder can tell whose PDN it is. Clearing a registration leaves the legacy fields and the session statistics set, and that stays as it is.
- **The consequence (owner accepts it).** A PDN passed on without a restart stays registered as the previous player. It keeps the previous player's record key. The new holder's matches are recorded and tagged as the previous player's, verify on the server, and count for the previous player.
- **F1 (editor's choice). The name is its own idle screen, the first one.** The idle screen list grows from eight to nine. The name screen is index `0`, and idle starts on it every time it is entered. It shows the label `"Player"` in the small font and the display name, up to `31` characters, in the large font over the full width, on up to three lines. It has no allegiance artwork, because the name needs the width. The name is broken at spaces. If text is left over after three lines, the whole name is broken again by filling each line as far as it goes. Only text that still does not fit three lines is cut. Because the font is proportional, a `31`-character name with many wide letters can still be cut. A PDN on the demo profile shows the demo profile's name, `Demo`. The name cannot share a statistics screen: the statistics column is about 8 small characters wide.
- **A stale scenario title.** The base scenario "Clicking primary or secondary button cycles forward through 8 idle screens" keeps its title, because a MODIFIED requirement keeps every base scenario title; its steps now cover nine screens. Tasks.md 7.3 renames it at archive.
- **F2 (editor's choice). No restart control is added.** Restarting a PDN means cycling its power.
- **F3 (editor's choice). Clearing also drops the totals baseline,** and `player-provider` "Clear registration session" lists exactly what clearing drops and leaves. It drops the server player UUID, name, team, affiliations, the mark that a profile is held, the PIN, the record key and the totals baseline. It leaves the legacy fields and the session statistics, as the owner ruled. Clearing runs only when the PIN entry screen is shown: at boot, after a failed lookup, and when a PDN stops copying its firmware to other PDNs. In each case no profile has been applied since the restart, so those fields hold nothing from an applied profile. The base text says clearing "resets identity to the unprovisioned nil UUID state"; the code clears the registration fields only (`lib/core/include/game/player.hpp:131`).

### Server totals on the statistics screens

What the server computes today for each player, over matches with `scoring_status = 'scoring'` in the active event: matches played, wins (the score is one point per win) and the average draw time in milliseconds. Losses are matches played minus wins. The server's draw time is the PDN's raw reaction time: the PDN writes its raw reaction time into the match record's draw time.

- **The rule (owner).** The PDN shows the player's server totals after registering, so a swap does not reset them. The server item goes last in the ranking, as a feature and not a security item, and must not hold up the roll-out gate (tag verification). The display has to handle totals that lag, and has to show something sensible when the server sends none.
- **T1 (editor's choice). Carrier.** The lookup response carries the totals as three optional fields beside the profile. A companion call is the server team's alternative. The firmware's placeholder fields on `DevicePlayerResponse` are `optional uint32 scoring_matches_played = 11`, `optional uint32 scoring_wins = 12` and `optional uint32 scoring_average_draw_time_ms = 13`, marked as provisional in the source, and they follow whatever names and numbers the server team chooses.
- **T2 (editor's choice). Absent totals are not an error.** A response with none, which is today's server and also a player with no scoring match, registers the PDN as usual with a baseline of `0`. This is unlike the record key, whose absence fails the registration, and it is why this item cannot hold up the roll-out. Each absent total counts as `0` on its own. A server sends all three or none; a response with only some shows odd numbers: matches `4` with no wins shows `4` losses, and matches with no average pulls the displayed average towards `0`.
- **T3 (editor's choice). Path to the PDN.** The totals live on the `Player` on both devices. The FDN copies the three numbers into the profile it sends the PDN over the radio. The packet has room. The numbers are not secret and nothing is decided from them: they are for display only and are never uploaded. A forged profile can show wrong numbers on one PDN and do nothing more.
- **T4 (editor's choice). One consistency rule.** Where a profile is applied (`Player::applyRegistrationProfile`), wins above matches played drop all three totals to `0`. So losses (matches minus wins) are never negative.
- **T5 (editor's choice). What each idle screen shows.** The baseline is the totals received at registration. The session is this PDN's own counters since it was restarted, as today.
  - Wins: baseline wins plus session wins.
  - Losses: baseline matches minus baseline wins, plus session losses.
  - Matches: baseline matches plus session matches.
  - Average reaction: baseline average times baseline matches, plus the session's total reaction time, divided by baseline matches plus session matches, rounded down; `0` when both counts are `0`. The arithmetic is 64-bit, so no product or sum of 32-bit totals wraps and the divisor is `0` only when both counts are. A reaction time from a match abandoned before it counted adds to the session total. With a baseline it is weighted in. With no baseline the screen shows `0` until a match counts, as today.
  - Streak and last reaction: session only, because the server has neither.
  - Posse and the symbol screen: unchanged.
- **T6 (editor's choice). The baseline is taken once, at registration,** and never refreshed during a session, so a match played this session is never counted twice. A PDN applies at most one profile between restarts.
- **T7 (editor's choice). Lag and mismatch are not hidden and not marked.** The server's totals hold only uploaded matches that the server scores.
  - A match neither PDN has uploaded yet is missing from the baseline until a later registration. After a swap from a dead PDN, that PDN's unsent matches are missing until it is revived near an FDN, or until the opponent's PDN uploads its copy.
  - The session counters include matches the server never scores. Some are not stored by the PDN: a shootout match ("Found, not changed") and a match against a demo profile. Others are stored and do not score: between teammates, with an unassigned player, outside a scoring period, or uploaded late into a closed period. So a displayed total can go down at the next registration.
  - The display does not mark any number as provisional.
- **T8 (editor's choice). Optional extra for the server team:** a current win streak, if they choose to compute one. It is in item 5 of the ranked server list. The firmware has no field for it in this change.

### Posse: only a supporter registered to a different player counts

- **The rule (owner).** A supporter counts only if its PDN is registered, and to a different player than the challenger it supports. The ruling says this needs the player UUID in the PDN-to-PDN connection context. A second PDN registered as another player still passes; that is the accepted two-PDN risk.
- **P1 (editor's choice). "Registered" here means a player UUID that is not all-zero,** which is a profile delivered by an FDN. A PDN on the demo profile and a PDN that has not registered do not count as supporters. A demo challenger can still be supported by registered PDNs. In the simulator each device has a different player; a simulated PDN started straight into play has no registration and does not count as a supporter.
- **P2 (editor's choice). Carrier: the posse confirmation, not the connection context.** The ruling named the connection context. This change uses the posse confirmation instead, because the context reaches only the cabled neighbour and indirect supporters are not the challenger's neighbours (`peer-comms-types.hpp:80-88`). The supporter's 16-byte player UUID travels in its confirmation, the frame each supporter sends straight to the challenger when its button is pressed for a round. The frame grows from `7` to `23` bytes. The challenger counts a confirmation only when that UUID is not all-zero and differs from its own player UUID. It logs each confirmation that fails the test. This is for the owner to confirm.
- **P3. Self-asserted.** The UUID in the confirmation is whatever the supporter's PDN says. A second PDN registered as another player passes (Risks).
- **P4 (editor's choice). Nothing else changes.** Membership, the cascade, the `15 ms` per supporter and the `270 ms` cap stay. The idle "Posse" screen still counts cabled members, eligible or not. A supporter is not told it did not count.

### Found, not changed

Places where the base specs and the code, or the base specs and this change, differ. This change alters no requirement text for them.

- **Player UUID in the connection context.** Base `player-provisioning` ("Cabled FDN-to-PDN profile delivery and wire identity") says the connection context and peer profile exchanged with cabled neighbours carry the player's 16-byte player UUID and team UUID. The code's `PlayerProfile` carries neither (`lib/core/include/device/drivers/peer-comms-types.hpp:80-88`).
- **Posse roster size.** `chain-roster` says up to `20` indirect supporters, with further posse-joins ignored when all slots are taken. The code has `18` slots and overwrites the oldest (`src/pdn/game/posse-manager.hpp:150`, `src/pdn/game/posse-manager.cpp:100-110`). `quickdraw-match` says `18` supporters and `270 ms`.
- **Shootout matches and the session statistics.** `shootout` says a shootout match must not write lifetime player statistics. `quickdraw-match` ("Post-match statistics, persistence, and result display") says the counters go up whenever a match resolves with a win or loss, and exempts a shootout match only from storage and sleep. The code follows `quickdraw-match`: the match result state counts every match with no shootout check (`src/pdn/game/quickdraw-states/match-result-state.cpp:18-27`), and that state has the shootout exits (`src/pdn/game/quickdraw-apps.cpp:182-188`). Those matches now also enter the displayed totals (T7).
- **The statistics sentence in base `player-provisioning`.** It says that if "career wins, losses, matches played, win streak, or reaction-time history" are provided "they SHALL be retained in the decoded profile; otherwise, session statistics SHALL initialize to `0`" (`openspec/specs/player-provisioning/spec.md:59`). It names a streak and a reaction history the server does not compute. It says "career" where the totals are the active event's scoring matches. Its "otherwise" reads as if provided statistics seed the session statistics; here they are a separate baseline and the session statistics start at `0` in every case. The sentence sits in a requirement `pdn-record-auth-and-relay` modifies, so it is not changed here.
- **"Credential" in base `fdn-apps`.** The requirement "Connected PDN provisioning vs. symbol-match routing" still says a player "presents their credential" and its scenario says "credential scan" (`openspec/specs/fdn-apps/spec.md:64`, `:69-70`). The PIN is an identifier, not a credential. `pdn-record-auth-and-relay` task 10.5 fixes the same word in the `fdn-apps` Purpose at archive.
- **"Without changing protobuf definitions" in base `player-types`.** The requirement "alleycat-server decodes into Player" says the codec decodes "without changing protobuf definitions or regenerating nanopb outputs" (`openspec/specs/player-types/spec.md:28`). The three placeholder fields (tasks.md 3.1) break that clause, as the record key field already does. `pdn-record-auth-and-relay` task 10.8 adds the `player-types` delta; tasks.md 3.8 makes it cover the three fields.

## Risks / Trade-offs

Accepted by the owner:

- **A PDN passed on without a restart is the previous player's.** The new holder's matches are recorded and tagged as the previous player's and count for them. The name on the idle screen is the only sign, it is shown only until the first click, and nothing forces a restart.
- **A second PDN registered as another player passes the supporter check.** One person with two PDNs can register the second as any player whose ID they have read and use it as their own supporter. This is the two-PDN risk the owner accepted in `pdn-record-auth-and-relay`.

Follow from the editor's choices, to confirm:

- **Displayed totals lag and can go down** (T5, T7). They miss matches not yet uploaded, and the session counters include matches the server never scores. A player may see a number fall after registering again.
- **Totals are whatever the profile says** (T3). They cross the radio in the clear and are not authenticated. A forged profile shows wrong numbers on one PDN. A response with only some totals shows odd numbers (T2).
- **A large total runs off the screen** (T5). The value column is about `40 px` wide and a 4-digit number in the large font is about `44 px` (computed from the font table, not rendered), so totals of `1000` or more are not shown whole.
- **The supporter check compares each supporter with the challenger only** (P2). Two supporters registered as the same player both count (Open Questions).
- **A supporter is not told that it did not count** (P4). A player cabled behind their own second PDN sees the same screens as a supporter that counted.

Follows from the owner's ruling:

- **Until the server returns totals, a swap still resets the four totals screens** to the new PDN's session counters.

Other risks:

- **Two wire formats change.** The profile packet and the posse confirmation both grow, and both are fixed layouts. A PDN or FDN on older firmware cannot register with one on this firmware. PDNs on builds before and after this change drop each other's posse confirmations, so a supporter on the other build never counts.
- **Some names are cut.** Because the font is proportional, a long name with many wide letters does not fit three lines even when broken anywhere, and its end is not shown (F1).

## Migration Plan

1. The firmware is built against the placeholder totals fields. It works against a server that sends none: every PDN then shows its session counters alone.
2. Every PDN and FDN takes the same build together, because the profile packet and the posse confirmation change. This can be the one roll-out of `pdn-record-auth-and-relay` or a later one. This firmware contains that change's firmware, so it is under the same gate: it is not flashed to event devices before the server verifies tags. It does not wait on the totals item.
3. When the server team has answered the totals item, the placeholder fields take the agreed names and numbers (tasks.md 5.2). If the team chooses a companion call instead, 5.2 also changes the two requirements that take the totals from the lookup response ("Scoring totals from the lookup" in `player-provisioning`, "Optional scoring totals are decoded" in `device-player-lookup`). A failed or slow companion call then counts as no totals and does not fail the registration, and both requests finish inside the FDN's `8000 ms` lookup limit (`lib/core/include/game/player-types.hpp:43`).
4. Whoever hands a PDN on is told the handover rule: restart it first, and check the name on its idle screen (tasks.md 7.1).

## Open Questions

**Ruled by the owner**

- The three rulings, under Decisions: handover, totals, posse.

**For the owner to answer**

- Do two supporters registered as the same player both count? Today's rule counts both, and so does this change.
- Who is told the handover rule: staff only, or players too?
- How is a PDN's power cycled at the event: a switch, or the battery? No spec says.

**Editor's choices, to confirm** (each is stated under Decisions)

- **F1.** The name is its own idle screen, the first one.
- **F2.** No restart control is added.
- **F3.** Clearing also drops the totals baseline.
- **T1.** Carrier: three optional fields on the lookup response.
- **T2.** Absent totals are not an error.
- **T3.** Path to the PDN: in the radio profile, display only.
- **T4.** One consistency rule: wins above matches drop the totals.
- **T5.** What each idle screen shows.
- **T6.** The baseline is taken once, at registration.
- **T7.** Lag and mismatch are not hidden and not marked.
- **T8.** A win streak is an optional extra for the server team.
- **P1.** "Registered" means a player UUID that is not all-zero.
- **P2.** Carrier: the posse confirmation, not the connection context the ruling named.
- **P4.** Nothing else about the posse changes.
