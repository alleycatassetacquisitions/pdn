## Context

Decided before this change and not reopened here:

- PDNs have no Wi-Fi. The network holds only FDNs and the server, with no device authentication between them; the server validates what it receives.
- Crash records and match records travel PDN -> FDN over the peer-to-peer radio, then FDN -> server over Wi-Fi. The PDN picks an FDN from its beacon (every `12,000 ms`), sends each record by reliable unicast, and advances its mark only when the FDN acknowledges having stored the record.
- Registration, symbol match and learning to play stay on the cable. A Connected FDN may still receive records; records never require the cable.
- Each PDN authenticates its own records with a short tag, not a 64-byte signature. The firmware-update signing key is not reused.

Decided by the owner on this proposal:

- The tag is 16 bytes.
- Every cable registration issues a fresh record key that replaces the one the PDN held.
- A fake FDN being able to make PDNs retire records is a known risk, accepted for now.
- Untagged crash records are accepted indefinitely and marked unverified. Match records are never sent untagged.
- An upload carries the sender's MAC address, the record kind, the raw record bytes, the trailer and the decoded fields.
- No eFuse key storage. Keys stay in ordinary flash.
- An FDN holds up to `32` relayed crash records, and nothing is relayed during a match or a shootout.
- The `Authorization: Bearer` wording is removed from the requirements this change touches.
- Anyone who registers a MAC address with a valid PIN, at an FDN's jack or from the FDN network, is issued a working key for that PDN without touching it. Accepted; the network path rests on the network holding only FDNs and the server (Risks).

What the code does today (top of the comms-rewrite stack):

- **Match records** go only to the FDN Connected on a jack. The PDN re-sends a `116`-byte frame (4-byte sequence number + `112`-byte match record) every 100 ms as a plain, unsequenced send until the FDN answers. The FDN checks the sender is Connected, writes the record to a 64-slot non-volatile spool, then acknowledges; the PDN advances its mark on that acknowledgement.
- **Crash records** have a radio sender that nothing uses. It broadcasts each pending `42`-byte record once per boot and advances the sent mark when the radio reports the broadcast transmitted. No FDN code receives, queues or acknowledges a crash record. The PDN build is instead wired to upload over Wi-Fi itself.
- **FDN uploads.** Match records upload on a five-minute idle timer. The FDN's crash upload runs once, at boot, although `fdn-apps` already asks for it on that timer as well.
- **Registration** runs between a PDN and the FDN it is Connected to, but every message travels over the radio in the clear: a 6-byte request carrying the PIN, and a 584-byte profile back. The PDN keeps nothing from registration across boots and registers again on every boot.
- **FDN lookups** use whatever scheme the configured server address has, and fall back to plain HTTP when it names none.
- **Serial jack**: carries only the 19-byte HELLO frame at 19200 baud. Its framing has an opcode byte, so a second frame type fits.
- **FDN beacon**: a broadcast every `12,000 ms`, but only while the FDN is on its idle or player-detected screen; it stops while a PDN is Connected. `fdn-apps` already asks for it whenever the radio is enabled. No PDN code listens for it.
- **Crypto in the PDN build**: HMAC-SHA-256 is available (mbedTLS message-digest layer, hardware SHA). ECDSA P-256 is compiled in; Ed25519 is not. The ESP32-S3 has no elliptic-curve accelerator.
- **Storage protection**: flash encryption, secure boot and NVS encryption are off in both builds. The SoC supports the eFuse-keyed HMAC peripheral, and nothing in the firmware uses an eFuse key block today.

## Goals / Non-Goals

**Goals:**

- A PDN that is never plugged in still delivers its crash records and match records.
- The server can tell that a record came from the holder of a record key issued for that MAC address, and that it was not altered on the way.
- A record is never retired on the PDN because a frame was merely sent.
- An FDN needs no secrets to relay.

**Non-Goals:**

- Authenticating FDNs to the server.
- Hiding record contents. Records travel in the clear as today.
- Proving a match happened. The tag proves who submitted a record, not that the opponent agrees. The server can compare the two PDNs' copies of one match ID.
- Proving a MAC address belongs to a particular physical PDN. A PDN has no identity from manufacturing; see "Whoever can reach the lookup" under Risks.
- Authenticating the FDN's beacon or its acknowledgement (see Risks).
- Removing PDN Wi-Fi itself. That is its own change; this one only stops depending on it.

## Decisions

### Tag scheme: per-PDN HMAC-SHA-256, truncated to 16 bytes

`tag = first 16 bytes of HMAC-SHA-256(record key, kind || PDN MAC || key ID || record bytes)`, with `kind` = `0x01` for a crash record and `0x02` for a match record. Every field has a fixed length for its kind, so the input is unambiguous. The record trailer is the key ID (1 byte) followed by the tag: `17` bytes per record.

Why 16 bytes. A guess can only be tested by submitting a record to the server, and each guess succeeds with chance 2^-128, so guessing is not a practical attack at any rate the server could serve. 16 bytes also meets RFC 2104 section 5 (at least 80 bits and at least half the hash length). The cost is `17` trailer bytes per record in FDN storage and in each upload. The length is fixed once devices ship: changing it later is another wire change.

Alternatives considered:

- **Per-PDN ECDSA P-256 key pair.** Signature `64` bytes raw (up to `72` DER-encoded); public key `64` bytes. In the build today. Signing runs in software on this chip; time per record not measured.
- **Per-PDN Ed25519 key pair.** Signature `64` bytes; public key `32` bytes. Needs a library the build does not have.

The trade-off, stated plainly:

- **With HMAC, the server holds every PDN's record keys.** A server breach lets the attacker forge records for every PDN, and recovery means dropping every key on the server and registering each PDN again. The key also has to reach the PDN in secret, at every registration: it crosses Wi-Fi inside TLS from the server to the FDN, sits in FDN memory for a moment, and crosses the serial jack.
- **With a key pair, the server holds only public keys and nothing on it can forge.** Nothing secret leaves the PDN, so registration needs no secret channel. The cost is 64 bytes per record instead of 16 in FDN storage and uploads, a software signature per record, and for Ed25519 a new library. Frame size does not rule it out: with a 64-byte signature in place of the 16-byte tag, a crash frame would be `113` bytes and a match frame `183`, against a `1469`-byte limit.
- **Neither fixes enrolment.** Either way the server has to decide which key belongs to a MAC address when one registers.

### The firmware-update key is not reused

Firmware images are signed with RSA-3072 and PDNs only verify (`firmware-distribution`, "Authenticity"). Every PDN can check that signature and none can produce one, and the release private key never goes on a device, so it cannot say which PDN wrote a record. Its signature block is also a 4096-byte sector, larger than a radio frame.

Where that key, or a second operations key handled the same way, could still help later:

- **Server-to-PDN data.** Anything the server wants PDNs to trust without a live connection (a signed configuration, a signed list of retired PDNs) could be signed once and verified by every PDN.
- **Certifying PDN keys.** If per-PDN key pairs are ever adopted, a certificate over each PDN's public key, signed at provisioning, would let the server accept a PDN it has never seen, which is the enrolment gap above.

Neither is part of this change. Today a PDN checks an update against the key that signed its running image through the platform's image verifier; using it for other data would need that public key exposed to the application.

### Key on the PDN

- **Issue.** The server issues a fresh record key at every registration. The PDN does not generate it.
- **Storage.** `32` bytes plus a 1-byte key ID in the PDN's ordinary non-volatile storage, in its own namespace, kept across boots. The player profile is discarded at every boot; the key cannot be, because crash records are offered before the PDN registers again.
- **Replaced at every registration.** A key accepted during registration replaces the stored one. This is how a lost, erased or damaged key is recovered: plug the PDN into an FDN and register; no operator is needed. The change is all-or-nothing, so a PDN that loses power during it still holds the old key or the new one. Who can cause a re-key is under Risks.
- **Use.** A tag is computed each time a record is sent, not when it is stored, so stored records need no extra bytes on the PDN. A frame the transport is still retrying keeps the trailer it was built with.
- **Match records wait for the key.** A PDN with no key holds its match records and sends them, tagged, once it has one. Otherwise a PDN would send them untagged on the first beacon after boot, seconds before registration hands it a key. Crash records are sent with or without a key: a crash before the first registration is still worth having.
- **A stolen PDN.** Flash encryption is off, so anyone with a USB cable can read the flash and find the record key, and whoever holds the PDN can also register it and be given a new one. Either way they can submit records as that one PDN until an operator blocks its MAC address on the server (below). They learn nothing about any other PDN's key, and once PDN Wi-Fi is removed the image holds no network credential.
- **No eFuse.** A record key burned into an eFuse block could never be replaced, so it cannot be combined with re-keying at registration. An eFuse key that only encrypts the storage holding a replaceable record key would still allow re-keying, but it is permanent per device and, without secure boot, only raises the bar from reading the flash to running code on the PDN.

### Registration: the key crosses the jack, not the radio

1. The PDN's registration request (radio, as today) is unchanged.
2. The FDN's lookup also names the Connected PDN's MAC address.
3. The server returns the profile as today and a fresh 32-byte record key with its key ID.
4. The FDN writes the record key to the serial jack in one new framed message, and then sends the profile over the radio as today. It accepts a key only from a lookup made over HTTPS with the server's certificate verified (so it must refuse a server address that is not `https`), holds it in volatile memory only, never logs or stores it, and erases it as soon as the frame is written to the jack or the PDN disconnects.
5. The PDN accepts a record key only from a jack on which an FDN is Connected, and only in the provisioning app after it has sent that FDN its own registration request. It discards a key that arrives damaged or malformed. An accepted key replaces the one it held.

What the FDN forwards to the server is only the PDN's MAC address. Nothing secret travels upward.

There is no confirmation on the jack. If the key frame is lost the PDN keeps the key it had, which the server also still keeps (below), or stays without one, in which case its match records wait. The next registration issues another. A lookup response with no key, from a server that does not issue them yet or for a blocked MAC address, leaves the PDN's key as it was. The jack already carries a HELLO frame every 20 ms from its own task, so the key frame has to be written without interleaving with one.

Why the jack: registration messages cross the radio unencrypted, so a key sent that way could be read by any radio nearby. The jack is the one link an eavesdropper cannot hear without touching the device. A 33-byte payload at 19200 baud takes about 20 ms. The serial framing belongs to the component that tracks jack connections; it should carry this as one opaque application frame, checked for length and CRC and handed up with the jack it arrived on, so the meaning of the bytes and the rules for accepting them stay with registration.

### Server key rules (proposal to the server team)

- **Issue at every registration.** Each lookup that names a PDN's MAC address returns a fresh key under the next key ID for that address. IDs run `1` to `255` and then start again at `1`; `0` is never issued.
- **Keep the key most recently issued under each key ID**, and verify a record under the key its key ID names. Earlier keys are still needed after a re-key: the key frame can be lost on the jack, so a PDN may still hold an earlier key; and records already stored on an FDN were tagged under the key the PDN held when it sent them. A key stops verifying when its ID comes round again, `255` issues later, or when an operator drops it.
- **Records tagged under an old key.** A record still pending on the PDN is tagged under the new key when it is next sent. A record already on an FDN carries the old key ID and verifies under that key. A record sent under the old key and sent again under the new one takes two of the FDN's slots until it uploads, and reaches the server twice with the same record bytes; it is one record (Signed content and replay).
- **Re-keying recovers; it does not revoke.** Earlier keys keep verifying, and anyone who can register a MAC address gets a new one. An operator has two tools. *Dropping* a MAC address's keys cuts off a copied key: the PDN, back with its owner, gets a working key at its next registration, the key IDs carry on from where they were, and records under the dropped keys fail their tags, both those still on FDNs and everything the PDN sends until it next registers. *Blocking* a MAC address stops the server issuing it keys and accepting its records; it is the only way to shut out a stolen PDN.
- **Answer only the FDN network.** The lookup and the uploads should not be reachable from anywhere else. Accepting that anyone on that network can be issued a key (Risks) depends on it.
- **Watch the issue rate.** Alert on a burst of issues for one MAC address, because `255` issues push out the key a PDN holds (Risks). A hard limit would let whoever can reach the lookup lock a PDN out of getting a key. The server can log the PIN and the source address behind each issue, but whoever asks chooses both, so the log is a hint and not proof.

### Signed content and replay

- **Signed:** the record kind, the sending PDN's MAC address, the key ID, and the record bytes exactly as sent (`42` bytes for a crash record, `112` for a match record). The record number in the frame is transport bookkeeping and is not signed.
- **The record identity is inside the signed bytes:** the crash number for a crash record, the match ID for a match record. With the MAC address in the tag, a record cannot be moved to another PDN; with the kind byte, a crash tag cannot be passed off as a match tag.
- **Replay:** a replayed frame is byte-identical to one the server has. The server treats the same PDN and crash number, or the same PDN and match ID, with the same record bytes, as one record, so a replay changes nothing. No counter or timestamp is needed on the device.
- **A consequence:** the two record layouts are now signed formats. Changing either means a new record kind, not an edit in place.
- **Crash numbers restart** if a PDN's storage is erased, so a later crash can reuse a number. "Same number, different bytes" is a different record; both the FDN and the server keep both.

### Frame size

One record frame serves both kinds: the transport's sequence ID, the record kind, a 4-byte record number, the record bytes, the trailer.

| Frame | Today | With this change | Limit |
|---|---|---|---|
| Crash record | `42` bytes | `65` bytes (1 + 1 + 4 + 42 + 17) | `1469` bytes |
| Match record | `116` bytes | `135` bytes (1 + 1 + 4 + 112 + 17) | `1469` bytes |

Both stay single frames with room to spare. The radio does not constrain the tag length.

### Relay: one frame, one acknowledgement, event-driven

- **One envelope.** Crash records and match records use the same record frame and the same stored acknowledgement (kind, number and record check). The FDN reads the kind from the frame, not from its length, so a later change to either record does not touch the intake.
- **Target.** The FDN that caused the send: the sender of the beacon just heard, or the FDN that just became Connected. Nothing is remembered between beacons, so there is no timer to tune, and a PDN plugged into an FDN that is full or busy still delivers to another one it can hear.
- **Triggers.** A beacon heard, an FDN becoming Connected, or a stored acknowledgement arriving. On the first two the PDN sends the lowest pending record of each kind; on the third, the next record of that kind. A record with no acknowledgement is simply sent again on the next beacon.
- **Kinds are independent.** A crash record stuck behind a full FDN queue does not hold back match records.
- **Transport.** Record frames go by the existing reliable unicast, which retries until the radio reports delivery or gives up. That covers a lost frame within a second or two; beacons cover everything longer. The match path moves onto it from its 100 ms re-send loop. Each kind gets its own packet type for the frame and for the acknowledgement, sharing one layout: the transport fixes one frame length per packet type, and a newer send to the same recipient on the same type replaces an older one that is still retrying.
- **In flight.** At most one record of each kind. A beacon that arrives while one is still retrying does nothing for that kind.
- **Acknowledgement.** An application message sent after the record is in non-volatile storage. It carries the kind, the number and a record check, the first 8 bytes of SHA-256 over the bytes and trailer the FDN stored. The PDN compares it with the frame as it last sent it, so an acknowledgement for some other frame under the same number does not count. The check only identifies the stored frame; it is not keyed and proves nothing about who acknowledged. Only an acknowledgement for the PDN's lowest pending record, from the FDN it was last sent to, moves the mark and releases the next record; anything else is ignored and a mark never moves backwards.
- **Not during play.** Nothing is sent while a match or a shootout is active, and a send still retrying when one starts is cancelled, so record traffic cannot disturb reaction timing. An acknowledgement that arrives mid-match still moves the mark; the next record waits for the next beacon.
- **One owner on each side.** On the PDN, one relay component at the level of the game session owns the target, the triggers and the play gate; the crash log and the match store only answer "what is your next pending record" and "advance your mark". On the FDN, one record store generalised from the match spool holds both kinds, with a capacity per kind.

The gaps in the current crash path and what closes each:

| Gap | Fix |
|---|---|
| The PDN retires a record when the radio reports the broadcast transmitted | The mark moves only on the FDN's stored acknowledgement |
| One attempt per boot | Every FDN beacon and every FDN connection sends again |
| Nothing on the FDN receives or queues crash records | The FDN stores them (32 records) and acknowledges after storing |
| The FDN uploads crash records only at boot, and its upload timer starts only when match records are waiting | Relayed and local crash records also upload on the periodic uplink, as `fdn-apps` already requires; the boot-time upload stays |
| The FDN stops beaconing while a PDN is Connected | The beacon runs whenever the radio is enabled, as `fdn-apps` already requires |
| The PDN build constructs its crash reporter for Wi-Fi upload | It is constructed on the relay path; the boot-time Wi-Fi upload goes |

### FDN behaviour: relay, do not verify

The FDN holds no record keys, so it cannot verify a tag and does not try. What it can check without a key, it checks: the frame has a known kind and the length of a frame of that kind (anything else is dropped), there is room, and it does not already hold a byte-identical copy from that sender. A frame that reuses a held record's number with different bytes is stored separately: treating it as a duplicate would let a junk frame absorb the real one.

It does not rate-limit by sender: a MAC address is trivial to forge, so a per-sender limit would stop nobody and could be turned against a real PDN. Capacity is the limit.

### Server check and responses (proposal to the server team)

Each upload of a relayed record carries the sender's MAC address, the record kind, the raw record bytes, the record trailer (key ID and tag), and the decoded fields the upload carries today. The server looks up the key for that MAC address and key ID, recomputes the tag over the kind, MAC address, key ID and raw bytes, and compares in constant time. It reads the record's contents from the decoded fields, and must reject an upload in which a decoded field, or a MAC address given anywhere else in the upload, disagrees with what the tag covers. The tag covers only the kind, the MAC address, the key ID and the raw bytes; anything not checked against them is unauthenticated.

The server judges the trailer first and looks for a stored copy second.

| Case | Crash record (`POST /device-logs`) | Match record (`POST /quickdraw-matches`) |
|---|---|---|
| Tag verifies under a kept key | accept as verified; it replaces an unverified copy of the same record bytes | accept |
| Tag does not verify: bad tag, key ID not kept, key ID `0`, or no raw bytes or trailer | accept, marked unverified; it never replaces a verified record | per-entry rejection with a new error code; keep in quarantine |
| Same record bytes as a stored record that is verified, or of the same standing | accept, store nothing new | accept, store nothing new |
| A decoded field or MAC address disagrees with what the tag covers | `422`; keep in quarantine | per-entry rejection; keep in quarantine |
| Blocked MAC address | `422` | per-entry rejection |

- **Unverified crash records are accepted indefinitely.** They come from a PDN that crashed before its first registration, one used only in demo mode, one whose storage was erased, one holding a key the server does not keep, and from FDNs themselves, whose own crash records carry no trailer because an FDN has no record key. Anyone in radio range or on the network can also create them under any MAC address. So the server must keep unverified records out of anything that counts or alerts per PDN. It should bound its unverified and quarantined stores in total, discard the oldest when one is full, and still answer success; a flood can then displace real unverified or quarantined records. A cap per MAC address would not help, for the reason given under FDN behaviour.
- **A crash record with a bad tag is kept as unverified, not rejected.** Rejecting it would stop no forger, who would send key ID `0` instead, and would lose real crash records from a PDN whose key is not one the server keeps.
- **A match record without a valid tag is rejected** and quarantined. A key can be issued for any MAC address, including one no PDN has, so the server should also flag for review a verified match record whose MAC address was not last registered by one of its two players. Flag, not reject: a PDN keeps pending matches across a change of player.
- **`422` matters** because the FDN stops its upload on an error and keeps the failed record. Without a "this will never succeed" answer one bad record would block the queue for good. Match uploads already retire a batch that comes back with per-entry rejections, and unknown error codes already map to a placeholder.
- **Quarantine matters** because by then the PDN has advanced its mark. A rejected record is gone from the devices; the server's copy is the only one an operator could recover.

## Risks / Trade-offs

- **A fake FDN can swallow records. Known, accepted for now, to be revisited.** Any device in range can broadcast a beacon and send stored acknowledgements; the PDN advances its mark and never resends. This is new: before, only a Connected FDN could acknowledge. The records stay in the PDN's ring buffers until overwritten and crash records can still be dumped over USB, but nothing rewinds a mark. Two ways to close it later: give each FDN a secret or signed identity a PDN can check; or have the server answer each record with a receipt tagged under that PDN's record key, relayed back by any FDN, with the PDN keeping the record until the receipt arrives. The second needs no FDN secret but does nothing for a PDN without a key. Revisit if records go missing in the field.
- **Whoever can reach the lookup can be issued a key for any MAC address. Decided and accepted.** Someone with a PDN in hand and an FDN to plug it into can re-key it; that is the intended recovery. The same door is open wider. The lookup takes the MAC address on the FDN's word and the network has no device authentication. So a modified device on an FDN's jack claiming another PDN's MAC address, a stolen FDN (its flash is unencrypted and holds the Wi-Fi password), or anything else on the FDN network can, with any valid PIN (four digits, sent over the radio in the clear), be issued a key for any PDN without touching it. With that key they can submit records that verify as that PDN. By causing `255` issues they can also push out the key the real PDN holds, after which its match records are rejected and its crash records arrive unverified until it registers again. A rule that a stored key is never replaced would protect a PDN that already holds one; re-keying at every registration gives that up. The tag therefore holds against someone who has only a radio, and not against someone who can reach the lookup. A radio alone can spoof a plugged-in PDN's registration request and cause an issue, but the key goes to the jack, not to the radio. → Accepted, not prevented. There are two paths. At an FDN's jack, a device that claims a PDN's MAC address and gives a valid PIN is issued its key; that is accepted outright. From the FDN network the same takes only a lookup; that is accepted on the condition that the network holds only FDNs and the server and that the server answers the lookup only on that network. Three things limit the damage once the server key rules are agreed (tasks.md 1.3). An alert on a burst of issues for one MAC address catches a push-out; it does not catch a single issue, which looks like an ordinary registration. Dropping a MAC address's keys removes a key issued to someone else until they ask again, and costs the real PDN its records until it registers again. Blocking a MAC address stops issues and records for it, the real PDN's included. Not adopted: issuing a key for an already-keyed MAC address only on proof that the PDN holds one of its keys; it would bring back an operator reset for a PDN that has lost its key. Reopen this if the network stops being FDNs and the server only: another kind of device joins it, the lookup becomes reachable from outside it, an FDN goes missing, or the Wi-Fi password is known outside it. The condition is not met yet: every PDN image built so far holds the Wi-Fi password (Migration Plan).
- **A fake FDN on the jack can replace a PDN's key** with one the server does not hold. Until the PDN registers at a real FDN its match records are rejected and its crash records arrive unverified. A PDN cannot tell a real FDN from a device that claims to be one.
- **Junk can keep an FDN's storage full.** Junk leaves the FDN at each upload (junk match records are rejected, junk crash records are accepted as unverified) and can be refilled at once, so one radio can hold an FDN full for as long as it transmits while PDN ring buffers overwrite. Each acknowledgement to a new MAC address also takes a slot in the FDN's radio peer table, and each beacon from a new address takes one in the PDN's. → Bounded on the devices by capacity and the driver's eviction rule, and on the server by the total bounds above.
- **Every PDN in range answers the same beacon.** Replies can collide; the transport retries and the next beacon is another chance.
- **A rejected match record is lost to the devices.** The PDN advanced its mark when the FDN stored it. → Server quarantine, above.
- **A PDN used only in demo mode never gets a key**, because the demo PIN never contacts the server. Its crash records stay untagged. A demo player's matches are not stored (`match-records-nil-player`), so no match records are held back.
- **The FDN handles a key at every registration.** A compromised FDN firmware could copy keys as they pass.

## Migration Plan

1. **Server first.** Accept the new fields on both uploads and the lookup, issue keys, and verify tags.
2. **Firmware together.** Frame formats change, so every FDN and PDN takes the same build, as with the other wire changes in the comms rewrite.
3. **PDNs registered before keys existed** need nothing done to them. A PDN registers on every boot, so each one receives a key the first time it registers at an upgraded FDN, and a fresh one at every registration after. Match records it was already holding wait for the first key and are sent tagged.
4. **Untagged crash records** are accepted indefinitely and marked unverified, so there is no cut-over date. Match records need no such allowance because a PDN never sends one untagged.
5. **Rollback.** The server can go back to ignoring the trailer at any time. Firmware rollback means reflashing every device.
6. **Closing the network.** The accepted lookup risk assumes only FDNs and the server are on the network. Today every PDN image holds the Wi-Fi password and PDN flash can be read over USB. The assumption holds only after PDN Wi-Fi is removed (its own change) and the Wi-Fi password is then changed, which means rebuilding every FDN. Until both are done, anyone holding a PDN can reach the lookup.

## Open Questions

None.
