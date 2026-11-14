## 1. Decisions

- [x] 1.1 Archive `pdn-pin-entry-fdn-registration` first; this change builds on the PDN sending its PIN to the FDN
- [x] 1.2 Owner answered Open Questions 1a and 1b: once the venue Wi-Fi password is changed the server is reachable only from the venue network, by FDNs and operators (the owner's statement of fact, not verified by us); and the unauthenticated player endpoints, the device lookup and `POST /quickdraw-matches` are accepted as they are
- [x] 1.3 Owner answered Open Question 2: neither (a) nor (b); this firmware is not rolled out to event devices until the server verifies tags, and FDNs accept match records over the radio as designed
- [x] 1.4 Owner answered Open Question 3: no FDN delay after a wrong PIN
- [x] 1.5 Owner answered Open Question 4: the record key is held in volatile memory only and lost at restart; the core-dump exception is accepted
- [x] 1.6 Owner answered Open Question 5: registration fails when no key arrives, so a PDN with no key is not registered and no match record without a key exists
- [ ] 1.7 Send the ranked server list (design.md) to the server team as a proposal, with the match record byte layout, the key and tag formulas and the test vector
- [x] 1.8 Owner ruled after the answers above: the key is derived from the server secret and the player alone; the lookup request is unchanged; the sender's MAC address on a match upload is a troubleshooting note only; blocking is by player only; one person registering two PDNs as two different players is an accepted risk
- [ ] 1.9 Owner confirms the editor's choices E1 to E6 (design.md, Open Questions)

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

- [ ] 4.1 Record frame: remove the `17`-byte trailer; a crash record frame is `48` bytes and a match record frame is `134` bytes, ending in the `16`-byte tag
- [ ] 4.2 Record check: the first 8 bytes of SHA-256 over the record bytes and, for a match record, the tag appended to them
- [ ] 4.3 FDN record store entries: remove the key ID from every entry and the tag from crash entries; keep the tag on match entries
- [ ] 4.4 Update the relay, intake and record store tests of the three PRs to the new frames, and add: a crash frame with trailing bytes is dropped as a wrong length; an acknowledgement computed over the same match bytes and a different tag is ignored

## 5. Record key delivery

- [ ] 5.1 FDN reads the record key from the lookup response, behind a marked seam until the server team agrees the key field's format; the lookup request is unchanged; the `player-provisioning` scenarios in which a lookup response carries a record key are met only when 10.3 fills the seam
- [ ] 5.2 FDN accepts no record key from a lookup that was not made over HTTPS with the certificate verified, and none that is not exactly `32` bytes; with no accepted key it sends the existing lookup-failure notification and no profile
- [ ] 5.3 Add one opaque application frame to the serial jack framing, checked for length and CRC and handed up with the jack it arrived on; write it without interleaving with HELLO frames
- [ ] 5.4 FDN delivers the key over the jack only and sends the profile only after the key is written; when the key cannot be written it sends the lookup-failure notification and no profile; it holds the key in volatile memory only and erases it once written or on disconnect
- [ ] 5.5 FDN display: `"UNABLE TO"` / `"ISSUE KEY"` for `3000 ms` when a lookup returned a profile and no key the FDN could deliver
- [ ] 5.6 PDN accepts a key only from a jack with an FDN Connected and only in the provisioning app after sending that FDN its PIN; it discards a key whose frame fails its integrity check or that is not `32` bytes; it holds the key in volatile memory, writes it to neither its key-value storage nor its match ring, sends it to no device and never logs or displays it
- [ ] 5.7 Wipe working copies of the key after use on the PDN and the FDN, and keep the key off task stacks where practical, because a core dump written to flash on a crash can contain it
- [ ] 5.8 PDN applies a profile only when it holds a key accepted during the same registration attempt; a profile that arrives with no key held is not applied and the PDN shows its existing failure screen and returns to PIN entry; a key accepted later in the same attempt replaces the held one; the key is dropped whenever an attempt ends without a profile applied
- [ ] 5.9 Tests register a player through one shared helper that applies the profile and sets a fixed test key; every test that expects a stored match uses it; the lookup-response test encoders gain the key when 10.3 fills the seam
- [ ] 5.10 Tests: key delivered over the jack before the profile; a key offered over the radio, or before the PDN has sent its PIN, is ignored; a response with no key, or with a malformed key, ends in the lookup-failure notification, no profile and the `"ISSUE KEY"` display; a key from a lookup without a verified certificate is not accepted and no profile is delivered; a profile with no key held is not applied on the PDN; the FDN erases the key on disconnect; no key is in the PDN's key-value storage or its match ring; the PDN holds no key after a restart; a second key frame before the profile replaces the held key; the key is dropped when no profile follows it

## 6. Tag at match end

- [ ] 6.1 Add HMAC-SHA-256 over the portable SHA-256 in lib/core so native tests and the PDN build share it
- [ ] 6.2 When a match ends, compute the tag over the `112` record bytes under the held key and store it in the record's ring slot (`128` bytes); when no key is held, store no record
- [ ] 6.3 Never send a slot holding `112` bytes and no tag, and do not let it hold back the pending records after it
- [ ] 6.4 Send each match record with its stored tag unchanged; registration, a failed registration and a restart neither clear nor re-tag stored records
- [ ] 6.5 Tests: the fixed test vector in `record-authentication`, shared with the server team; no record is stored when no key is held; the stored tag is sent unchanged after a restart, by a PDN that is not registered, and after registering to another player; a `112`-byte slot is not sent and the record after it is; a crash record frame carries no tag

## 7. FDN uploads

- [ ] 7.1 Upload local and relayed crash records at boot and on the periodic uplink, so the once-per-boot latch no longer blocks later uploads; start the uplink when crash records are waiting, not only match records
- [ ] 7.2 Upload the FDN's own crash records first, then the relayed ones; a failure on an own record stops the own records for that cycle and does not stop the relayed ones
- [ ] 7.3 Upload a relayed crash record in today's device-log shape under the originating PDN's MAC address, with no new field
- [ ] 7.4 Retire a relayed crash record on success and on `400`, `413`, `415` or `422`, and continue with the next; on any other failure stop the relayed records and keep them for the next cycle; the FDN's own records advance only on success
- [ ] 7.5 Add one marked seam in the match upload encoder for the raw `112` record bytes, the `16`-byte tag and the sender's MAC address as a troubleshooting note; send today's shape until the server team agrees the field format; met only when 10.3 fills the seam: the scenario "Uploaded match carries the record bytes, the tag and the sender note alongside the decoded fields" (`match-collection`), and in `record-relay` the upload sentence of "FDN relays without verifying" with the upload part of its scenario "A record with a wrong tag is still relayed", and the requirement "Sender's MAC address on a match upload is a note only"
- [ ] 7.6 Tests: a relayed crash record uploads under the PDN's MAC address; relayed crash records upload on the periodic uplink after the boot upload; each of `400`, `413`, `415` and `422` retires the record and the next uploads; `503` stops the relayed records and keeps both; `400` on an own record leaves it pending and the relayed record still uploads; the seam is given the record bytes, the tag and the sender's MAC address exactly as stored; a bad tag is relayed unchanged

## 8. Same-player match refusal

- [ ] 8.1 The inviting PDN refuses a match acknowledgement that names its own non-nil player UUID: it clears the pending match and does not enter the countdown
- [ ] 8.2 Store no match record when the two player UUIDs in it are equal
- [ ] 8.3 Tests: an acknowledgement naming the inviting PDN's own player UUID clears the pending match and starts no countdown; a match that resolves with equal player UUIDs in both slots stores no record; the invited side's existing refusal still holds; a nil player UUID on the inviting PDN is not refused

## 9. Server (proposals to the server team, in rank order; outside this repository)

- [ ] 9.1 Record the server team's answer on item 1: keep both copies of a match and flag contradicted and unconfirmed ones
- [ ] 9.2 Record the server team's answer on item 2: the lookup response returns the record key for the looked-up player; match uploads carry the raw bytes and the tag, and are verified; the sender's MAC address is kept as a troubleshooting note only
- [ ] 9.3 Record the server team's answer on item 3: optional winner, for ties
- [ ] 9.4 Record the server team's answer on item 4: block a player

## 10. Migration and specs

- [ ] 10.1 One firmware roll-out: the same build on every FDN and PDN, only after the server has built and enabled item 2, so that it returns keys and verifies tags
- [ ] 10.2 After PDN Wi-Fi is removed and before the server returns keys, change the venue Wi-Fi password and rebuild every FDN; until then anyone holding a PDN can join the network
- [ ] 10.3 Before the roll-out: when the server team has agreed item 2's formats, fill the lookup-response and upload seams (5.1, 7.5)
- [ ] 10.4 Before the server enables verification and before a PDN is flashed, upload that PDN's stored matches with the firmware it has; a match slot written by older firmware is never sent afterwards
- [ ] 10.5 At archive, edit the Purpose paragraphs of `crash-reporting`, `match-collection` and `fdn-apps`, which still describe cabled collection and cannot be changed by a delta
- [ ] 10.6 Owner updates the threat model, which is kept outside this repository, with the entries under Risks in design.md
- [ ] 10.8 Every firmware PR description of this change states that the stack must not be flashed to event devices before the server verifies tags

## 11. Verification

- [ ] 11.1 On devices, against a test server that returns keys: a PDN that is never plugged in after registration delivers a crash record and a match record to an FDN in radio range, and the server stores both
- [ ] 11.2 On devices: an FDN switched off mid-transfer loses no record; the PDN delivers on a later beacon
- [ ] 11.3 On devices: register a PDN and capture the radio; the record key does not appear in any frame
- [ ] 11.4 On devices: a PDN Connected to an FDN in symbol match still hears beacons and delivers
- [ ] 11.5 On devices, against a test server that returns a key: register, play a match, capture the match record frame, and check its tag equals the first 16 bytes of HMAC-SHA-256 under that key over the frame's `112` record bytes
- [ ] 11.6 On devices, against a test server that returns no key: registration fails; the FDN shows `"UNABLE TO"` / `"ISSUE KEY"` and the PDN returns to PIN entry
