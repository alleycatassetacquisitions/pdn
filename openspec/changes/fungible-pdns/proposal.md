## Why

PDNs are fungible: a player often swaps to another PDN during the event when a battery dies or a PDN crashes, and the PDN left behind may then be used by someone else. The owner has ruled on three things that follow from that: how a PDN changes hands, what its statistics screens show after a swap, and who may count as a posse supporter.

These three are a handover rule, a display feature that needs its own server item, and a game rule. None of them is part of record authentication, so they are not in `pdn-record-auth-and-relay`, and none of them may hold up that change's roll-out gate, which is the server verifying record tags.

## What Changes

- **Handover.** A PDN is handed to another player by restarting it. A restart returns it to PIN entry with no registered profile and no record key. No sign-out is added and no restart control is added: restarting means cycling the PDN's power. A PDN passed on without a restart stays registered as the previous player, and the new holder's matches are recorded and tagged as the previous player's and count for them. The owner accepts this.
- **Name on idle.** The hub idle state gains a ninth screen, the first one, that shows the player's display name. Idle starts on it every time it is entered. After one click the name is up to eight clicks away, and it is shown again only when idle is next entered.
- **Clearing a registration** is specified as it works today, plus dropping the totals baseline this change adds. It drops the registration profile, the PIN, the record key and the baseline. It leaves the legacy fields (name, id, role, allegiance, faction, symbol), which the game reads and which are copied from a profile when one is applied, and the session statistics. The owner ruled that leaving them stays as it is.
- **Server totals.** The lookup response may carry the player's totals from the server: matches played, wins and average draw time, over the matches the server scores. The FDN passes them to the PDN in the profile. The PDN keeps them as a baseline taken once at registration and adds its own session counters to them on the wins, losses, matches and average reaction screens. Streak and last reaction stay session-only, because the server has neither.
- **No totals is not an error.** A response with no totals registers the PDN as usual with a baseline of `0`. Today's server sends none.
- **Totals lag and can go down.** The server's totals hold only uploaded matches that it scores. A match not yet uploaded is missing until a later registration. The session counters also count matches the server never scores: matches the PDN does not store (a shootout match, a match against a demo profile) and stored matches that do not score (between teammates, with an unassigned player, outside a scoring period, or uploaded late into a closed period). The display does not mark this.
- **Posse supporters.** A supporter adds to its challenger's boost only if its PDN is registered to a player and that player is not the challenger's. The supporter's player UUID travels in its posse confirmation. Posse membership, the cascade, the `15 ms` per supporter and the `270 ms` cap do not change.
- **BREAKING** (wire format): the profile an FDN sends a PDN grows by the three totals, and the posse confirmation grows from `7` to `23` bytes by the supporter's player UUID. PDNs on builds before and after this change drop each other's confirmations. Every PDN and FDN takes the same build together.

## Capabilities

### New Capabilities

None.

### Modified Capabilities
- `player-provisioning`: three ADDED requirements: handing a PDN to another player by restarting it; scoring totals taken from the lookup; scoring totals delivered with the profile and kept as a baseline. The two requirements `pdn-record-auth-and-relay` modifies are not touched.
- `device-player-lookup`: one ADDED requirement: the optional scoring totals are decoded, and their absence is not an error.
- `player-provider`: "Clear registration session" states exactly what clearing drops and what it leaves.
- `pdn-session-routing`: "Hub awaken sequence and idle screen behavior" has nine idle screens, the player's name first, and says what each totals screen adds up.
- `chain-roster`: "Posse roles, challenger cascade, and supporter boost" carries the supporter's player UUID in the confirmation and counts only eligible supporters.

## Impact

- **Depends on** `pdn-record-auth-and-relay`: the record key that a restart drops comes from there. What "registered" means for a supporter is P1 in design.md. This change is written with all of that change's firmware applied, so its firmware is under the same roll-out gate.
- **PDN firmware**: a ninth idle screen with wrapped text; a totals baseline on the player the PDN is playing as; the totals sums on four idle screens; the supporter's player UUID in the posse confirmation and the challenger's check.
- **FDN firmware**: three placeholder fields in the firmware's copy of the lookup response, decoded when present; three numbers added to the profile packet.
- **Server (outside this repository)**: one item, "Return the player's totals with the lookup", ranked last in the server list of `pdn-record-auth-and-relay` (design.md, "Ranked server list (proposal to the server team)", item 5). It is a feature, not a security item, and does not gate the roll-out. Until it exists every PDN shows its session counters alone.
- **Operations**: whoever hands a PDN on restarts it first. Nothing in the firmware forces it. Who is told the rule, and how a PDN's power is cycled, are open questions for the owner.
- **Found, not changed**: places where the base specs and the code, or this change, differ are recorded in design.md, "Found, not changed". This change alters no requirement text for them.
