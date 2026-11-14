## 1. Decisions

- [x] 1.1 Archive `pdn-pin-entry-fdn-registration` first; this change builds on the PDN sending its PIN to the FDN
- [ ] 1.2 Owner answers Open Questions 1a and 1b: the server is reachable only by FDNs and operators once the Wi-Fi password is changed; and every player endpoint that returns a PIN (`GET /players`, `GET /players/{id}`, `GET /players/by-pin/{pin}`), the device lookup, and `POST /quickdraw-matches` are acceptable as they are
- [ ] 1.3 Owner answers Open Question 2: FDNs keep taking match records only from a Connected PDN until the server verifies tags (a), or the exposure in between is accepted (b); on (a), add that condition to `record-relay` and the FDN intake
- [ ] 1.4 Owner answers Open Question 3: the FDN's delay after an unknown PIN and its numbers
- [ ] 1.5 Owner answers Open Question 4: the record key in volatile memory only, lost at restart
- [ ] 1.6 Owner answers Open Question 5: a PDN with no key sends matches with the all-zero tag, which never count once the server verifies
- [ ] 1.7 Send the ranked server list and its precondition (design.md) to the server team as a proposal, with the match record byte layout, the key and tag formulas and the test vector

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

- [ ] 5.1 FDN lookup names the Connected PDN's MAC address, behind a marked seam until the server team agrees the format; the scenario "Lookup names the Connected PDN" is met only when 10.3 fills the seam
- [ ] 5.2 FDN accepts no record key from a lookup that was not made over HTTPS with the certificate verified; it discards a key that is not exactly `32` bytes and still delivers the profile
- [ ] 5.3 Add one opaque application frame to the serial jack framing, checked for length and CRC and handed up with the jack it arrived on; write it without interleaving with HELLO frames
- [ ] 5.4 FDN delivers the key over the jack only, before it sends the profile, holds it in volatile memory only, and erases it once written or on disconnect
- [ ] 5.5 PDN accepts a key only from a jack with an FDN Connected and only in the provisioning app after sending that FDN its PIN; it discards a key whose frame fails its integrity check or that is not `32` bytes; it holds the key in volatile memory, writes it to neither its key-value storage nor its match ring, sends it to no device and never logs or displays it
- [ ] 5.6 PDN replaces the held key with one accepted later in the same session, and drops the key when it leaves registration without a profile applied
- [ ] 5.7 Wipe working copies of the key after use on the PDN and the FDN, and keep the key off task stacks where practical, because a core dump written to flash on a crash can contain it
- [ ] 5.8 Tests: key delivered over the jack before the profile; a key offered over the radio, or before the PDN has sent its PIN, is ignored; a response with no key still registers the PDN; a malformed key is discarded on the FDN and on the PDN; a key from a lookup without a verified certificate is not accepted; the FDN erases the key on disconnect; no key is in the PDN's key-value storage or its match ring; the PDN holds no key after a restart; a second key frame before the profile replaces the held key; the key is dropped when no profile follows it

## 6. Tag at match end

- [ ] 6.1 Add HMAC-SHA-256 over the portable SHA-256 in lib/core so native tests and the PDN build share it
- [ ] 6.2 When a match ends, compute the tag over the `112` record bytes under the held key, or use `16` zero bytes when no key is held, and store it in the record's ring slot (`128` bytes)
- [ ] 6.3 Read a slot holding `112` bytes and no tag as carrying the all-zero tag
- [ ] 6.4 Send each match record with its stored tag, whether or not it is all zero
- [ ] 6.5 Tests: the fixed test vector in `record-authentication`, shared with the server team; all-zero tag without a key; the stored tag is sent unchanged after a restart and after registering to another player; a `112`-byte slot is sent with the all-zero tag; a crash record frame carries no tag

## 7. FDN uploads

- [ ] 7.1 Upload local and relayed crash records at boot and on the periodic uplink, so the once-per-boot latch no longer blocks later uploads; start the uplink when crash records are waiting, not only match records
- [ ] 7.2 Upload the FDN's own crash records first, then the relayed ones; a failure on an own record stops the own records for that cycle and does not stop the relayed ones
- [ ] 7.3 Upload a relayed crash record in today's device-log shape under the originating PDN's MAC address, with no new field
- [ ] 7.4 Retire a relayed crash record on success and on `400`, `413`, `415` or `422`, and continue with the next; on any other failure stop the relayed records and keep them for the next cycle; the FDN's own records advance only on success
- [ ] 7.5 Add one marked seam in the match upload encoder for the sender's MAC address, the raw `112` record bytes and the `16`-byte tag; send today's shape until the server team agrees the field format; met only when 10.3 fills the seam: the scenario "Uploaded match carries the sender, the record bytes and the tag alongside the decoded fields" (`match-collection`), and in `record-relay` the upload sentence of "FDN relays without verifying" with the upload part of its scenarios "A record with a wrong tag is still relayed" and "A match record with the all-zero tag is still relayed"
- [ ] 7.6 Tests: a relayed crash record uploads under the PDN's MAC address; relayed crash records upload on the periodic uplink after the boot upload; each of `400`, `413`, `415` and `422` retires the record and the next uploads; `503` stops the relayed records and keeps both; `400` on an own record leaves it pending and the relayed record still uploads; the seam is given the MAC address, record bytes and tag exactly as stored; a bad tag is relayed unchanged

## 8. FDN delay after an unknown PIN

- [ ] 8.1 After a lookup answered with `404`, make no lookup for a delay: `2,000 ms` after the first, doubling with each further `404` up to `120,000 ms`, none again after `600,000 ms` without a `404`; FDN-wide, in volatile memory, unchanged by a successful lookup
- [ ] 8.2 During a delay, answer a PIN request the FDN would otherwise look up with the lookup-failure notification without contacting the server
- [ ] 8.3 FDN display: `"LOOKUPS"` / `"PAUSED"` for `3000 ms` on a request answered during a delay
- [ ] 8.4 Tests: `2,000 ms` after the first `404`; doubling to `4,000`, `8,000` and `16,000 ms`; the cap at `120,000 ms` from the seventh `404`; a request during a delay gets the failure notification, reaches no server and does not extend the delay; lookups resume when the delay ends; the delay is none again after `600,000 ms` without a `404`; a successful lookup changes nothing; a timeout or `503` is not counted; the delay applies to a PDN on the other jack; a restart clears it; the paused display

## 9. Server (proposals to the server team, in rank order; outside this repository)

- [ ] 9.1 Record the server team's answer on the precondition: every player endpoint that returns a PIN (`GET /players`, `GET /players/{id}`, `GET /players/by-pin/{pin}`), the device lookup, and `POST /quickdraw-matches` are not reachable by players
- [ ] 9.2 Record the server team's answer on item 1: keep both copies of a match and flag contradicted and unconfirmed ones
- [ ] 9.3 Record the server team's answer on item 2: the lookup takes the PDN's MAC address and returns the derived key; match uploads carry the sender's MAC address, the raw bytes and the tag, and are verified
- [ ] 9.4 Record the server team's answer on item 3: optional winner, for ties
- [ ] 9.5 Record the server team's answer on item 4: rate-limit the PIN lookup
- [ ] 9.6 Record the server team's answer on item 5: block a PDN or a player

## 10. Migration and specs

- [ ] 10.1 First roll-out: one firmware build on every FDN and PDN; no server change is needed first
- [ ] 10.2 After PDN Wi-Fi is removed and before the server returns keys, change the venue Wi-Fi password and rebuild every FDN; until then anyone holding a PDN can join the network
- [ ] 10.3 Second roll-out, to every FDN: when the server team has agreed item 2's formats, fill the lookup and upload seams (5.1, 7.5); this can be the rebuild of 10.2
- [ ] 10.4 Agree with the server team that the lookup returns keys first with verification off, and that verification is turned on between events or at an announced time; until then copies with the all-zero tag count
- [ ] 10.5 At archive, edit the Purpose paragraphs of `crash-reporting`, `match-collection` and `fdn-apps`, which still describe cabled collection and cannot be changed by a delta
- [ ] 10.6 Owner updates the threat model, which is kept outside this repository, with the entries under Risks in design.md

## 11. Verification

- [ ] 11.1 On devices: a PDN that is never plugged in delivers a crash record and a match record to an FDN in radio range, and the server stores both
- [ ] 11.2 On devices: an FDN switched off mid-transfer loses no record; the PDN delivers on a later beacon
- [ ] 11.3 On devices: register a PDN and capture the radio; the record key does not appear in any frame
- [ ] 11.4 On devices: a PDN Connected to an FDN in symbol match still hears beacons and delivers
- [ ] 11.5 On devices, against a test server that returns a key: register, play a match, capture the match record frame, and check its tag equals the first 16 bytes of HMAC-SHA-256 under that key over the frame's `112` record bytes
- [ ] 11.6 On devices, against a test server that returns no key: restart that PDN, register, play a match, capture the frame, and check its tag is all zero
- [ ] 11.7 On devices: enter unknown PINs at one FDN; the FDN shows `"LOOKUPS"` / `"PAUSED"` for a request during a delay, and the server receives no more than one lookup per `120,000 ms` once the delay has reached its cap
