## 1. Decisions

- [x] 1.1 Archive `pdn-pin-entry-fdn-registration` first; this change builds on the PDN sending its PIN to the FDN
- [x] 1.2 Owner answered Open Questions 1a and 1b: once the venue Wi-Fi password is changed the server is reachable only from the venue network, by FDNs and operators (the owner's statement of fact, not verified by us); and the unauthenticated player endpoints, the device lookup and `POST /quickdraw-matches` are accepted as they are
- [x] 1.3 Owner answered Open Question 2: neither (a) nor (b); this firmware is not rolled out to event devices until the server verifies tags, and FDNs accept match records over the radio as designed
- [x] 1.4 Owner answered Open Question 3: no FDN delay after a wrong PIN
- [x] 1.5 Owner answered Open Question 4: the record key is held in volatile memory only and lost at restart; the core-dump exception is accepted
- [x] 1.6 Owner answered Open Question 5: registration fails when no key arrives, so a PDN with no key is not registered and no match record without a key exists
- [ ] 1.7 Send the ranked server list (design.md) to the server team as a proposal, with the match record byte layout, the key and tag formulas and the test vector
- [x] 1.8 Owner ruled after the answers above: the key is derived from the server secret and the player alone; the lookup request is unchanged; the sender's MAC address on a match upload is a troubleshooting note only; blocking is by player only; one person registering two PDNs as two different players is an accepted risk
- [x] 1.9 Editor's choices closed (design.md, Open Questions): E1, E2, E3, E5, E6, E7 and E8 confirmed by the chief (E8: host names and validity dates not checked, redirects refused, one pinned server key); E4 replaced by E10, answered by the owner; X-2 confirmed by the chief: no fourth relay trigger; X-3 answered by the owner (section 13); E11 answered by the owner: no reaction-time floor (task 13.6)

## 2. Relay on the PDN (built by the open PRs)

- [x] 2.1 One relay component at the level of the game session owns the triggers, the target and the play gate; the crash log and the match store expose only "next pending record" and "advance mark" (#427)
- [x] 2.2 Listen for beacons that identify an FDN; send on a beacon, on an FDN connection and on a stored acknowledgement; keep at most one record of each kind in flight (#427)
- [x] 2.3 Send nothing during a match or a shootout, and cancel a send in flight when one starts (#427)
- [x] 2.4 Crash records as a relay source: sent by reliable unicast on their own packet type; the mark advances only on a stored acknowledgement for the lowest pending record from the FDN it was last sent to (#427)
- [x] 2.5 Match records as a relay source, on their own packet type, under the same rule (#428)
- [x] 2.6 Remove the crash log's broadcast sender, its send-status handler and the boot-time Wi-Fi crash upload from the PDN build; construct the PDN's crash reporter on the relay path (#427)
- [x] 2.7 Remove the match store's 100 ms re-send loop and its Connected-FDN condition (#428)

## 3. FDN record store, intake and beacon (built by the open PRs)

- [x] 3.1 The beacon identifies its sender as an FDN and runs in every state while the radio is enabled (#426)
- [x] 3.2 One record store for both kinds, generalised from the match spool, holding `32` crash records and `64` match records (#426)
- [x] 3.3 Accept record frames from any PDN; drop an unknown kind or wrong length; acknowledge after storing, with the record check; acknowledge a byte-identical duplicate without storing; store a same-number, different-bytes frame separately; no acknowledgement when full (#426)

## 4. Rework of #426, #427 and #428: no key ID, no crash trailer

- [x] 4.1 Record frame: remove the `17`-byte trailer; a crash record frame is `48` bytes and a match record frame is `134` bytes, ending in the `16`-byte tag (#435)
- [x] 4.2 Record check: the first 8 bytes of SHA-256 over the record bytes and, for a match record, the tag appended to them (#435)
- [x] 4.3 FDN record store entries: remove the key ID from every entry and the tag from crash entries; keep the tag on match entries (#435)
- [x] 4.4 Update the relay, intake and record store tests of the three PRs to the new frames, and add: a crash frame with trailing bytes is dropped as a wrong length; an acknowledgement whose check does not cover the tag is ignored (#435)

## 5. Record key delivery

- [x] 5.1 FDN reads the record key from the lookup response: the firmware's copy of the lookup response gains the provisional field `optional bytes record_key = 10`, marked `SERVER-SEAM(pdn-record-auth-and-relay)`, and follows whatever name and number the server team chooses (10.3); the lookup request is unchanged (#442)
- [x] 5.2 FDN accepts no record key from a lookup that was not made over HTTPS to a server whose certificate carries the pinned public key (configured server address `https://`; host name and validity dates not checked; redirects refused), and none that is not exactly `32` bytes; a key of another length is no usable key, not an undecodable response; with no accepted key it sends the existing lookup-failure notification and no profile (#442). The pinned key itself is built in `neoband-registration` tasks.md section 2, which reworks #442 in place
- [x] 5.3 Add one opaque application frame to the serial jack framing, checked for length and CRC and handed up with the jack it arrived on; write it without interleaving with HELLO frames (#436)
- [x] 5.4 FDN delivers the key over the jack only and sends the profile as soon as the jack reports the key frame written, with no added wait; when the jack does not report it written (the jack refuses the frame, or the PDN is no longer Connected on it) the FDN sends no profile, and sends the lookup-failure notification only if the PDN is still Connected; it holds the key in volatile memory only and erases it once written or on disconnect (#442)
- [x] 5.5 FDN display: `"UNABLE TO"` / `"ISSUE KEY"` for `3000 ms` when a lookup returned a profile and no key the FDN accepts, or the jack did not report the key frame written; when the PDN that asked has unplugged and no PDN remains Connected on either jack the FDN leaves the registration screen at once instead, as today (#442)
- [x] 5.6 PDN accepts a key only from a jack with an FDN Connected and only in the provisioning app after sending that FDN its PIN; it discards a key whose frame fails its integrity check or that is not `32` bytes; it holds the key in volatile memory, writes it to neither its key-value storage nor its match ring, sends it to no device and never logs or displays it (#441)
- [x] 5.7 Wipe working copies of the key after use on the PDN and the FDN, and keep the key off task stacks where practical, because a core dump written to flash on a crash can contain it; design.md "Key on the PDN" lists what is and is not overwritten (#441, #442)
- [x] 5.8 PDN applies a profile delivered by an FDN only once it holds both that profile and a key accepted during the same registration attempt, in either order; a profile that arrives first is held until the key arrives; when the attempt's existing `10,000 ms` limit ends with either missing the attempt fails as a timeout does today and both are dropped; a lookup-failure notification fails the attempt at once, and so does the FDN the PIN was sent to disconnecting from that jack, whatever the PDN holds; a key accepted later in the same attempt replaces the held one; the demo profile is applied with no key, as before (#441)
- [x] 5.9 Test helpers: one shared helper registers a player by applying the profile and setting a fixed test key, and every test that expects a stored match uses it (#443); the helpers that deliver a profile to a PDN (`makeProvisionPacket` in `test/test_core/player-fetch-tests.hpp`, `cliDeliverCabledProvision` in `test/test_cli/native-driver-tests.hpp`) also deliver a key frame on the fake jack (#441); the lookup-response test encoders carry the key in the provisional field (#442)
- [ ] 5.10 Tests: the profile is sent once the jack reports the key frame written; a profile that reaches the PDN before the key is held and applied when the key arrives; a profile with no key by the end of the `10,000 ms` limit is not applied and is dropped; the FDN disconnecting from the jack ends the attempt at once, with a key held and with a profile held; a key offered over the radio, or before the PDN has sent its PIN, is ignored; a response with no key, or with a malformed key, ends in the lookup-failure notification, no profile and the `"ISSUE KEY"` display; a key from a lookup without a verified certificate is not accepted and no profile is delivered; a jack that does not report the key frame written ends in no profile, with the lookup-failure notification when the PDN is still Connected and none when it has disconnected, and with the `"ISSUE KEY"` display unless no PDN remains Connected on either jack; the FDN erases the key on disconnect; no key is in the PDN's key-value storage or its match ring; the PDN holds no key after a restart; a second key frame before the profile replaces the held key; the key is dropped when no profile follows it; the demo profile is applied with no key (most are in #441 and #442; still to write: the `"ISSUE KEY"` screen text, a key offered over the radio, the FDN's key erased on disconnect, the demo profile with no key)

## 6. Tag at match end

- [x] 6.1 Add HMAC-SHA-256 over the portable SHA-256 in lib/core so native tests and the PDN build share it (#438)
- [x] 6.2 When a match ends, compute the tag over the `112` record bytes under the held key and store it in the record's ring slot (`128` bytes) in the namespace `"match_ring"`; when no key is held, store no record (#443)
- [x] 6.3 Keep matches stored by older firmware: read the namespace `"matches"` in place, never write or clear it (delete the `seq == 0` clear); send a `112`-byte legacy ring slot as kind `0x02` with a tag of `16` zero bytes and a `128`-byte one as written, advancing that namespace's own `"collectedSeq"`; skip and log once any other length; send each JSON `"match_<i>"` as kind `0x03` with record number `i`, erase it on its stored acknowledgement and `"count"` and `"test_key"` when none remain, and keep and log once a string that does not parse (#443)
- [x] 6.4 Send each match record with its stored tag unchanged; registration, a failed registration and a restart neither clear nor re-tag stored records; legacy records are pending only while the current ring has none, legacy ring before JSON (#443)
- [x] 6.5 Tests: the fixed test vector in `record-authentication`, shared with the server team; no record is stored when no key is held; the stored tag is sent unchanged after a restart, by a PDN that is not registered, and after registering to another player; a `112`-byte legacy slot is sent with a zero tag and its acknowledgement advances the legacy mark; a JSON match is sent as kind `0x03` with the wire layout and its key is erased only on acknowledgement; a JSON string that does not parse is kept; a new match is sent before any pending legacy record; booting with legacy data erases nothing; a crash record frame carries no tag (#443)
- [x] 6.6 FDN: accept kind `0x03` into the match store; send no stored acknowledgement for a kind `0x03` frame while `32` kind `0x03` entries are held; tests for the cap, and that a current match is still accepted at the cap (#443)
- [ ] 6.7 FDN: hold kind `0x03` entries, not uploaded, behind `SERVER-SEAM(legacy-match)`; once the server team gives a field or route for JSON-era matches (PINs, no player UUIDs), upload them through it and retire them on success

## 7. FDN uploads

- [x] 7.1 Upload everything waiting in one upload cycle, at boot and on the periodic uplink, so the once-per-boot latch no longer blocks later uploads; start the uplink when crash records are waiting, not only match records; a PIN lookup and an upload cycle never hold Wi-Fi together (#437)
- [x] 7.2 Upload the match batch first, then the FDN's own crash records, then the relayed ones; a failure on an own record, including a sent mark that cannot move, stops the own records for that cycle and does not stop the relayed ones (#437)
- [x] 7.3 Upload a relayed crash record in today's device-log shape under the originating PDN's MAC address, with no new field (#437)
- [x] 7.4 Retire a relayed crash record on success and on `400`, `413`, `415` or `422`, and continue with the next; on any other failure stop the relayed records and keep them for the next cycle; the FDN's own records advance only on success (#437)
- [x] 7.5 Add one marked seam in the match upload encoder for the raw `112` record bytes, the `16`-byte tag and the sender's MAC address as a troubleshooting note; send today's shape until the server team agrees the field format (#437; the seam is unfilled by design); met only when 10.3 fills the seam: the scenario "Uploaded match carries the record bytes, the tag and the sender note alongside the decoded fields" (`match-collection`), and in `record-relay` the upload sentence of "FDN relays without verifying" with the upload part of its scenario "A record with a wrong tag is still relayed", and the requirement "Sender's MAC address on a match upload is a note only"
- [ ] 7.6 Tests: a relayed crash record uploads under the PDN's MAC address; relayed crash records upload on the periodic uplink after the boot upload; each of `400`, `413`, `415` and `422` retires the record and the next uploads; `503` stops the relayed records and keeps both; `400` on an own record leaves it pending and the relayed record still uploads; one cycle uploads every kind in order, the match batch first; an own record whose mark cannot move is not repeated in the cycle; a cycle and a lookup do not hold Wi-Fi together; the seam is given the record bytes, the tag and the sender's MAC address exactly as stored; a bad tag is relayed unchanged (all but the last two are in #437; those two need the seam filled, 10.3)

## 8. Same-player match refusal

- [x] 8.1 The inviting PDN refuses a match acknowledgement that names its own non-nil player UUID: it clears the pending match and does not enter the countdown (#439)
- [x] 8.2 Store no match record when the two player UUIDs in it are equal (#439)
- [x] 8.3 Tests: an acknowledgement naming the inviting PDN's own player UUID clears the pending match and starts no countdown; a match that resolves with equal player UUIDs in both slots stores no record; the invited side's existing refusal still holds; a nil player UUID on the inviting PDN is not refused (#439)

## 9. Server (proposals to the server team, in rank order; outside this repository)

- [ ] 9.1 Record the server team's answer on item 1: keep both copies of a match and flag contradicted and unconfirmed ones
- [ ] 9.2 Record the server team's answer on item 2: the lookup response returns the record key for the looked-up player; match uploads carry the raw bytes and the tag, and are verified; the sender's MAC address is kept as a troubleshooting note only
- [ ] 9.3 Record the server team's answer on item 3: optional winner, for ties
- [ ] 9.4 Record the server team's answer on item 4: block a player
- [ ] 9.5 Record the server team's answer on item 5: return the player's totals with the lookup (a feature, ranked last; it does not gate the roll-out; the firmware side is in the `fungible-pdns` change)

## 10. Migration and specs

- [ ] 10.1 One firmware roll-out: the same build on every FDN and PDN, only after the server has built and enabled item 2, so that it returns keys and verifies tags
- [x] 10.2 Remove Wi-Fi from the PDN build in a PR of this stack (no spec delta: the base specs already say a PDN uses no Wi-Fi) (#444)
- [ ] 10.3 Before the roll-out, when the server team has agreed item 2's formats: move the provisional key field of the lookup response (5.1) to the agreed name and number, and fill the match upload seam (7.5)
- [ ] 10.4 No upload before flashing is required; advise operators that a PDN uploaded with its old firmware before the server verifies gets its stored matches counted, while after flashing they arrive untagged and never count once verification is on
- [ ] 10.5 At archive, edit the Purpose paragraphs of `crash-reporting`, `match-collection` and `fdn-apps`, which still describe cabled collection and cannot be changed by a delta; the Purpose paragraph of `player-provisioning`, which calls the PIN a "player credential" although it is an identifier printed on the player's ID; and the `fdn-apps` Purpose, which says a player "scans their credential". The `fdn-apps` requirement "Connected PDN provisioning vs. symbol-match routing" says "credential" too; it is recorded in `fungible-pdns` design.md, "Found, not changed"
- [ ] 10.6 Owner updates the threat model, which is kept outside this repository, with the entries under Risks in design.md
- [x] 10.7 Every firmware PR description of this change states that the stack must not be flashed to event devices before the server verifies tags (#426, #427, #428, #435 to #444)
- [ ] 10.8 The archived `device-player-lookup`, `quickdraw-match-upload` and `player-types` specs describe today's lookup and upload. Add deltas against them to this change, with the capabilities listed in proposal.md, as each part lands: with section 5, the record key returned beside the `Player` (`device-player-lookup` hands the caller only a `Player`, and a response with no usable key now fails the registration); with 10.3, the raw `112` record bytes, the `16`-byte tag and the sender's MAC address note added to the fields `quickdraw-match-upload` says each match is encoded with; and the clause in `player-types` that the server types change "without changing protobuf definitions", which the key field breaks (section 5 has landed in #441 and #442, so the first part, the `device-player-lookup` delta, is due now)
- [ ] 10.9 Owner, at the roll-out: change the venue Wi-Fi password and build the FDN images with the new one; this gates nothing; until the password is changed anyone holding a PDN flashed so far can join the network (split from 10.2)

## 11. Verification

- [ ] 11.1 On devices, against a test server that returns keys: a PDN that is never plugged in after registration delivers a crash record and a match record to an FDN in radio range, and the server stores both
- [ ] 11.2 On devices: an FDN switched off mid-transfer loses no record; the PDN delivers on a later beacon
- [ ] 11.3 On devices, against a test server that returns keys: register a PDN and capture the radio; the record key does not appear in any frame
- [ ] 11.4 On devices: a PDN Connected to an FDN in symbol match still hears beacons and delivers
- [ ] 11.5 On devices, against a test server that returns a key: register, play a match, capture the match record frame, and check its tag equals the first 16 bytes of HMAC-SHA-256 under that key over the frame's `112` record bytes
- [ ] 11.6 On devices, against a test server that returns no key: registration fails; the FDN shows `"UNABLE TO"` / `"ISSUE KEY"` and the PDN returns to PIN entry
- [ ] 11.7 On devices: a PDN flashed from deployed firmware with stored JSON matches, and one holding a `112`-byte slot from a bench build, keep them after flashing; each new match is delivered first, then the legacy records, untagged; the FDN holds the kind `0x03` records

## 12. FDN record store in its own storage area (new PR on top of this stack)

- [ ] 12.1 Add a data partition for the FDN record store at `0x7E1000`, size `0x1F000` (124 KB, the free end of flash after `nvs_keys`), in `partitions.csv`
- [ ] 12.2 Move the FDN record store into that partition, keeping `64` match records, `32` relayed crash records and the `32`-entry legacy ring inside the `64`-match budget; tests that the limits hold
- [ ] 12.3 Check whether the OTA path can, or must, refuse an image built for a different partition table, and record the answer in design.md, Migration Plan, step 9
- [ ] 12.4 Reflash every FDN over USB with the new partition table before the roll-out; OTA cannot change it

## 13. X-3: forged standalone match results (owner's ruling; firmware on top of #457)

- [ ] 13.1 (a) Draw-window check: in `MatchManager`, for standalone matches only, refuse a `DRAW_RESULT` or `NEVER_PRESSED` before the local draw window opens (the local start time is set), before any write; `LOG_W`, no state change, nothing sent; every match keeps the `>= 0` check; no minimum raw time (E11)
- [ ] 13.2 (a) Tests: refused before the window, never-pressed refused before the window, an honest `3 ms` press in a standalone match scored, shootout result at `10 ms` still accepted; the boost scenario uses raws of `90` and `100 ms`
- [ ] 13.3 (b) Match key: `utils/hardware-random.hpp` (`esp_fill_random` on the device); serial application frame kind `0x03` (`MATCH_KEY`) beside the record key's `0x02`, threaded through the parser, hand-off and coordinator; payload is the binary match ID plus the 16-byte key; `AwaitBandState` ignores other kinds
- [ ] 13.4 (b) Frame signing: `MatchPacket` gains a `uint32_t` counter and a 16-byte tag (`static_assert` 103 bytes); the Hunter sends the key before `SEND_MATCH_ID` and clears the match if the jack refuses it; the Bounty holds the key from its opponent jack's peer and pairs it with the initiation once that peer is a Connected PDN; an admit filter in `ReliableChannel` checks tag, counter and role before duplicate detection; the key and counters are wiped on every match clear; `MATCH_ROLE_MISMATCH` and shootout frames stay unsigned
- [ ] 13.5 (b) Tests: a forged result without the key is dropped and the honest result with the same sequence number is still accepted; replayed and reflected frames are dropped; a key from a non-opponent jack is ignored; a key that arrives while the link is connecting pairs once it connects; a refused key frame starts no match; the key is wiped on finalize and abandon and survives a reseat; parser and fuzz tests for kind `0x03`; registration still works with the record key frame
- [ ] 13.6 Owner ruled E11: no reaction-time floor, since the countdown is a fixed rhythm and an honest press can land at or near `0 ms`. Still to do: confirm on hardware with the `TIMING` logs that the countdown skew is below the radio latency of a `0 ms` press, so the draw-window check does not drop it
