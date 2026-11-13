## 1. Decisions and server contract

- [ ] 1.1 Archive `pdn-pin-entry-fdn-registration` first; this change builds on its registration request
- [ ] 1.2 Owner answers design.md Open Questions 1 to 4 and 6 to 8
- [ ] 1.3 Agree with the server team: lookup request and response fields for the record key; upload fields for MAC address, record kind, record bytes, key ID and record tag; whether the decoded fields stay (Open Question 5)
- [ ] 1.4 Agree the server's key rules (design.md, "Server behaviour this design assumes")
- [ ] 1.5 Agree the server's record rules (design.md, "Server check and responses"), including the limit on failed verifications per MAC address
- [ ] 1.6 Write the crash-record and match-record byte layouts into the server API contract as signed formats

## 2. Record key and tag on the PDN

- [ ] 2.1 Store the record key and key ID in their own non-volatile namespace, kept across boots; ignore a key offered while one is held
- [ ] 2.2 Add an operator command over USB that clears the record key
- [ ] 2.3 Compute the record tag at send time and append the record trailer
- [ ] 2.4 Hold match records while the key ID is `0`
- [ ] 2.5 Confirm HMAC-SHA-256 links in the PDN build with hardware SHA
- [ ] 2.6 Tests: tag vectors shared with the server team; key ID `0` trailer; match records held until a key arrives; a second key is ignored

## 3. Registration

- [ ] 3.1 PDN registration request reports the key ID
- [ ] 3.2 FDN lookup names the Connected PDN's MAC address and key ID; accept a record key only over HTTPS with the certificate verified, and refuse a server address that is not `https`; discard a malformed key and still deliver the profile
- [ ] 3.3 Add one opaque application frame to the serial jack framing, handed up with the jack it arrived on; write it without interleaving with HELLO frames
- [ ] 3.4 FDN delivers the key over the jack only, holds it in volatile memory only, and erases it once written or on disconnect
- [ ] 3.5 PDN accepts a key only from a jack with an FDN Connected, only in the provisioning app with its own request outstanding, and only when it holds none; discard a damaged or malformed key
- [ ] 3.6 Tests: key delivered over the jack; a key offered over the radio, or with no request outstanding, is ignored; a PDN that reports a key ID receives none; a malformed key is discarded; a key from a lookup without a verified certificate is not accepted; the FDN erases the key on disconnect

## 4. Relay on the PDN

- [ ] 4.1 Add one relay component at the level of the game session that owns the triggers, the target and the play gate; the crash log and the match store expose only "next pending record" and "advance mark"
- [ ] 4.2 Listen for beacons that identify an FDN; send on a beacon, on an FDN connection and on a stored acknowledgement; keep at most one record of each kind in flight; send nothing during a match or shootout and cancel a send in flight when one starts
- [ ] 4.3 Send both record kinds in the record frame by reliable unicast, one packet type per kind; advance a mark only on a stored acknowledgement whose kind, number and record check match the lowest pending record, from the FDN it was last sent to
- [ ] 4.4 Remove the crash log's broadcast sender and its send-status handler
- [ ] 4.5 Remove the match store's 100 ms re-send loop and its Connected-FDN condition
- [ ] 4.6 Construct the PDN's crash reporter on the relay path and remove the boot-time Wi-Fi crash upload from the PDN build
- [ ] 4.7 Tests: an unacknowledged record is sent again on the next beacon; a delivery report alone does not move a mark; an acknowledgement from another device, from an earlier FDN, or with the wrong record check is ignored; one kind does not hold back the other; nothing is sent during a match, a send in flight is cancelled when one starts, and an acknowledgement arriving mid-match moves the mark without sending; a burst of beacons does not multiply sends; a beacon that does not identify an FDN is ignored

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

## 7. Verification

- [ ] 7.1 On devices: a PDN that is never plugged in delivers a crash record and a match record to an FDN in radio range, and the server stores both
- [ ] 7.2 On devices: an FDN switched off mid-transfer loses no record; the PDN delivers on a later beacon
- [ ] 7.3 On devices: register a PDN and capture the radio; the record key does not appear in any frame
- [ ] 7.4 On devices: a PDN Connected to an FDN in symbol match still hears beacons and delivers
- [ ] 7.5 Optional, one sacrificial board: build with eFuse-keyed NVS encryption and confirm a flash dump no longer shows the record key
