## 1. Decisions

- [x] 1.1 Archive `pdn-pin-entry-fdn-registration` first; this change builds on the PDN sending its PIN to the FDN
- [ ] 1.2 Owner confirms the PIN limit numbers: `5` "no such PIN" answers within `600,000 ms`, then no lookups for `600,000 ms`, FDN-wide
- [ ] 1.3 Owner confirms the session-only record key and that a PDN without a key sends match records with the all-zero tag
- [ ] 1.4 Send the ranked server list (design.md) to the server team as a proposal, with the match record byte layout and the key and tag formulas

## 2. Relay on the PDN (built by the open PRs)

- [x] 2.1 One relay component at the level of the game session owns the triggers, the target and the play gate; the crash log and the match store expose only "next pending record" and "advance mark" (#427)
- [x] 2.2 Listen for beacons that identify an FDN; send on a beacon, on an FDN connection and on a stored acknowledgement; keep at most one record of each kind in flight (#427)
- [x] 2.3 Send nothing during a match or a shootout, and cancel a send in flight when one starts (#427)
- [x] 2.4 Crash records as a relay source: sent by reliable unicast on their own packet type; the mark advances only on a stored acknowledgement for the lowest pending record from the FDN it was last sent to (#427)
- [x] 2.5 Match records as a relay source, on their own packet type, under the same rule (#428)
- [x] 2.6 Remove the crash log's broadcast sender, its send-status handler and the boot-time Wi-Fi crash upload from the PDN build; construct the PDN's crash reporter on the relay path (#427)
- [x] 2.7 Remove the match store's 100 ms re-send loop and its Connected-FDN condition (#428)

## 3. FDN store, intake and beacon (built by the open PRs)

- [x] 3.1 The beacon identifies its sender as an FDN and runs in every state while the radio is enabled (#426)
- [x] 3.2 One record store for both kinds, generalised from the match spool, holding `32` crash records and `64` match records (#426)
- [x] 3.3 Accept record frames from any PDN; drop an unknown kind or wrong length; acknowledge after storing, with the record check; acknowledge a byte-identical duplicate without storing; store a same-number, different-bytes frame separately; no acknowledgement when full (#426)

## 4. Rework of #426, #427 and #428: no key ID, no crash trailer

- [ ] 4.1 Record frame: remove the `17`-byte trailer; a crash record frame is `48` bytes and a match record frame is `134` bytes, ending in the `16`-byte tag
- [ ] 4.2 Record check: the first `8` bytes of SHA-256 over the record bytes, followed by the tag for a match record
- [ ] 4.3 FDN store entries: remove the key ID from every entry and the tag from crash entries; keep the tag on match entries
- [ ] 4.4 Update the relay, intake and store tests of the three PRs to the new frames, and add: a crash frame with trailing bytes is dropped as a wrong length; an acknowledgement computed over the same match bytes and a different tag is ignored

## 5. Record key delivery

- [ ] 5.1 FDN lookup names the Connected PDN's MAC address, behind the same marked seam as the upload fields until the server team agrees the format
- [ ] 5.2 FDN accepts a record key only over HTTPS with the certificate verified, and refuses a server address that is not `https`; it discards a key that is not exactly `32` bytes and still delivers the profile
- [ ] 5.3 Add one opaque application frame to the serial jack framing, handed up with the jack it arrived on; write it without interleaving with HELLO frames
- [ ] 5.4 FDN delivers the key over the jack only, before it sends the profile, holds it in volatile memory only, and erases it once written or on disconnect
- [ ] 5.5 PDN accepts a key only from a jack with an FDN Connected and only in the provisioning app after sending that FDN its PIN; it discards a damaged key or one that is not `32` bytes; it holds the key in volatile memory only and never sends, logs or displays it
- [ ] 5.6 Tests: key delivered over the jack before the profile; a key offered over the radio, or before the PDN has sent its PIN, is ignored; a response with no key still registers the PDN; a malformed key is discarded on the FDN and on the PDN; a key from a lookup without a verified certificate is not accepted; the FDN erases the key on disconnect; no key is in the PDN's non-volatile storage; the PDN holds no key after a restart

## 6. Tag at match end

- [ ] 6.1 Confirm HMAC-SHA-256 links in the PDN build with hardware SHA
- [ ] 6.2 When a match ends, compute the tag over the `112` record bytes under the held key, or use `16` zero bytes when no key is held, and store it in the record's ring slot (`128` bytes)
- [ ] 6.3 Read a `112`-byte slot written by earlier firmware as carrying the all-zero tag
- [ ] 6.4 Send each match record with its stored tag, whether or not it is all zero
- [ ] 6.5 Tests: tag vectors shared with the server team; all-zero tag without a key; the stored tag is sent unchanged after a restart and after registering to another player; a `112`-byte slot is sent with the all-zero tag; a crash record frame carries no tag

## 7. FDN uploads

- [ ] 7.1 Upload local and relayed crash records on the periodic uplink as well as at boot, so the once-per-boot latch no longer blocks later uploads; start the uplink when crash records are waiting, not only match records
- [ ] 7.2 Upload a relayed crash record in today's device-log shape under the originating PDN's MAC address, with no new field
- [ ] 7.3 Retire a relayed crash record on success and on `400`, `413`, `415` or `422`, and continue with the next; on any other failure stop and keep the record for the next cycle
- [ ] 7.4 Add one marked seam in the match upload encoder for the sender's MAC address, the raw `112` record bytes and the `16`-byte tag; send today's shape until the server team agrees the field format
- [ ] 7.5 Tests: a relayed crash record uploads under the PDN's MAC address; each of `400`, `413`, `415` and `422` retires the record and the next uploads; `503` stops the cycle and keeps both; the seam is given the MAC address, record bytes and tag exactly as stored; a bad tag is relayed unchanged

## 8. FDN PIN lookup limit

- [ ] 8.1 Count, in volatile memory and FDN-wide, the lookups the server answers with `404`; after `5` within `600,000 ms` make no lookup for `600,000 ms` and answer each request with the lookup-failure notification without contacting the server
- [ ] 8.2 Tests: the fifth `404` stops lookups; a request during the stop gets the failure notification and no request reaches the server; lookups resume after `600,000 ms`; a successful lookup does not reset the count; `404` answers spread over more than `600,000 ms` do not stop lookups; a timeout or `503` is not counted; the stop applies to a PDN on the other jack

## 9. Server (proposals to the server team, in rank order; outside this repository)

- [ ] 9.1 Keep both copies of a match and flag contradicted and unconfirmed ones
- [ ] 9.2 The lookup takes the PDN's MAC address and returns the derived key; match uploads carry the sender's MAC address, the raw bytes and the tag, and are verified
- [ ] 9.3 Optional winner, for ties
- [ ] 9.4 Rate-limit the PIN lookup
- [ ] 9.5 Block a PDN or a player

## 10. Migration and specs

- [ ] 10.1 Roll out one firmware build on every FDN and PDN; no server change is needed first
- [ ] 10.2 When the server team agrees item 9.2, fill the lookup and upload seams (5.1, 7.4) with the agreed field format
- [ ] 10.3 After PDN Wi-Fi is removed, change the venue Wi-Fi password and rebuild every FDN; until then anyone holding a PDN can join the network
- [ ] 10.4 At archive, edit the Purpose paragraphs of `crash-reporting`, `match-collection` and `fdn-apps`, which still describe cabled collection and cannot be changed by a delta
- [ ] 10.5 Owner updates the threat model, which is kept outside this repository: record intake from any MAC address, the beacon handler on the PDN, the jack key frame, the derived key and the server secret, the FDN PIN limit, and the four accepted risks in design.md

## 11. Verification

- [ ] 11.1 On devices: a PDN that is never plugged in delivers a crash record and a match record to an FDN in radio range, and the server stores both
- [ ] 11.2 On devices: an FDN switched off mid-transfer loses no record; the PDN delivers on a later beacon
- [ ] 11.3 On devices: register a PDN and capture the radio; the record key does not appear in any frame
- [ ] 11.4 On devices: a PDN Connected to an FDN in symbol match still hears beacons and delivers
- [ ] 11.5 On devices: enter five unknown PINs at one FDN; the sixth request is answered at once with the failure screen and no lookup reaches the server for `600,000 ms`
