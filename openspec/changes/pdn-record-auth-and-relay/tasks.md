## 1. Decisions and server contract

- [ ] 1.1 Archive `pdn-pin-entry-fdn-registration` first; this change builds on its registration request
- [ ] 1.2 Agree with the server team: the lookup names the PDN's MAC address and returns a record key and key ID; each upload carries the MAC address, record kind, raw record bytes, key ID, record tag and the decoded fields
- [ ] 1.3 Agree the server's key rules (design.md, "Server key rules"); the accepted lookup risk depends on the lookup answering only the FDN network, the issue alert, and the drop and block actions
- [ ] 1.4 Agree the server's record rules (design.md, "Server check and responses")
- [ ] 1.5 Write the crash-record and match-record byte layouts into the server API contract as signed formats

## 2. Record key and tag on the PDN

- [ ] 2.1 Store the record key and key ID in their own non-volatile namespace, kept across boots; an accepted key replaces the stored one, and a power loss during the change leaves the old key or the new one
- [ ] 2.2 Compute the record tag at send time and append the record trailer
- [ ] 2.3 Hold match records while the key ID is `0`
- [ ] 2.4 Confirm HMAC-SHA-256 links in the PDN build with hardware SHA
- [ ] 2.5 Tests: tag vectors shared with the server team; key ID `0` trailer; match records held until a key arrives; a new key replaces the old one, including key ID `1` after `255`, and a pending record goes out under it; a power loss during the change leaves one whole key

## 3. Registration

- [ ] 3.1 FDN lookup names the Connected PDN's MAC address; accept a record key only over HTTPS with the certificate verified, and refuse a server address that is not `https`; discard a malformed key and still deliver the profile
- [ ] 3.2 Add one opaque application frame to the serial jack framing, handed up with the jack it arrived on; write it without interleaving with HELLO frames
- [ ] 3.3 FDN delivers the key over the jack only, before it sends the profile, holds it in volatile memory only, and erases it once written or on disconnect
- [ ] 3.4 PDN accepts a key only from a jack with an FDN Connected and only in the provisioning app after sending that FDN its request; discard a damaged or malformed key
- [ ] 3.5 Tests: key delivered over the jack; a key offered over the radio, or before the PDN has sent its request, is ignored; the key reaches the PDN before the profile; registering again delivers a new key; a response with no key leaves the PDN's key unchanged; a malformed key is discarded; a key from a lookup without a verified certificate is not accepted; the FDN erases the key on disconnect

## 4. Relay on the PDN

- [ ] 4.1 Add one relay component at the level of the game session that owns the triggers, the target and the play gate; the crash log and the match store expose only "next pending record" and "advance mark"
- [ ] 4.2 Listen for beacons that identify an FDN; send on a beacon, on an FDN connection and on a stored acknowledgement; keep at most one record of each kind in flight; send nothing during a match or shootout and cancel a send in flight when one starts
- [ ] 4.3 Send both record kinds in the record frame by reliable unicast, one packet type per kind; advance a mark only on a stored acknowledgement for the lowest pending record whose record check matches the frame as last sent (kept in memory, since a re-key changes the trailer), from the FDN it was last sent to
- [ ] 4.4 Remove the crash log's broadcast sender and its send-status handler
- [ ] 4.5 Remove the match store's 100 ms re-send loop and its Connected-FDN condition
- [ ] 4.6 Construct the PDN's crash reporter on the relay path and remove the boot-time Wi-Fi crash upload from the PDN build
- [ ] 4.7 Tests: an unacknowledged record is sent again on the next beacon; a delivery report alone does not move a mark; an acknowledgement from another device, from an earlier FDN, with the wrong record check, or for a frame sent under an earlier key is ignored; one kind does not hold back the other; nothing is sent during a match, a send in flight is cancelled when one starts, and an acknowledgement arriving mid-match moves the mark without sending; a burst of beacons does not multiply sends; a beacon that does not identify an FDN is ignored

## 5. FDN

- [ ] 5.1 Make the beacon identify its sender as an FDN, and keep it running in every state while the radio is enabled
- [ ] 5.2 Generalise the match spool into one record store for both kinds, with sender MAC address, kind, number, bytes and trailer per entry, `32` crash records and the existing match bound
- [ ] 5.3 Accept record frames from any PDN; drop an unknown kind or wrong length; acknowledge after storing, with the record check; acknowledge a byte-identical duplicate without storing; store a same-number, different-bytes frame separately; no acknowledgement when full
- [ ] 5.4 Upload local and relayed crash records on the periodic uplink as well as at boot, so the once-per-boot latch no longer blocks later uploads; start the uplink when crash records are waiting, not only match records
- [ ] 5.5 Upload the MAC address, record kind, record bytes, key ID and record tag unchanged with each record
- [ ] 5.6 Retire a relayed crash record on `422` and continue with the next
- [ ] 5.7 Tests: a record from a PDN that is not Connected is stored and acknowledged; full storage sends no acknowledgement; a bad tag is relayed unchanged; a junk frame under a real record's number does not displace it; an unknown kind or wrong length is dropped

## 6. Migration and specs

- [ ] 6.1 Roll out in order: server, then one firmware build on every FDN and PDN
- [ ] 6.2 At archive, edit the Purpose paragraphs of `crash-reporting`, `match-collection` and `fdn-apps`, which still describe cabled collection and cannot be changed by a delta
- [ ] 6.3 Owner updates the threat model, which is kept outside this repository: record intake from any MAC address, the beacon handler on the PDN, the jack key frame, record-key storage and re-keying, key issuance on the lookup and its forgeable log, a PDN's key pushed out by repeated issues, and unverified crash records accepted under any MAC address. Record the fake-FDN acknowledgement as a known, accepted risk to revisit, and key issuance to anyone who can reach the lookup as accepted, with its reopen condition
- [ ] 6.4 After PDN Wi-Fi is removed, change the Wi-Fi password and rebuild every FDN; until then anyone holding a PDN can reach the lookup

## 7. Verification

- [ ] 7.1 On devices: a PDN that is never plugged in delivers a crash record and a match record to an FDN in radio range, and the server stores both
- [ ] 7.2 On devices: an FDN switched off mid-transfer loses no record; the PDN delivers on a later beacon
- [ ] 7.3 On devices: register a PDN and capture the radio; the record key does not appear in any frame
- [ ] 7.4 On devices: a PDN Connected to an FDN in symbol match still hears beacons and delivers
