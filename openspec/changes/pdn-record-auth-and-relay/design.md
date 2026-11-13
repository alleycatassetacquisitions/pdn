## Context

Decided before this change and not reopened here:

- PDNs have no Wi-Fi. The network holds only FDNs and the server, with no device authentication between them; the server validates what it receives.
- Crash records and match records travel PDN -> FDN over the peer-to-peer radio, then FDN -> server over Wi-Fi. The PDN picks an FDN from its beacon (every `12,000 ms`), sends each record by reliable unicast, and advances its mark only when the FDN acknowledges having stored the record.
- Registration, symbol match and learning to play stay on the cable. A Connected FDN may still receive records; records never require the cable.
- Each PDN authenticates its own records with a short tag, not a 64-byte signature. The firmware-update signing key is not reused.

What the code does today (top of the comms-rewrite stack):

- **Match records** go only to the FDN Connected on a jack. The PDN re-sends a `116`-byte frame (4-byte sequence number + `112`-byte match record) every 100 ms as a plain, unsequenced send until the FDN answers. The FDN checks the sender is Connected, writes the record to a 64-slot non-volatile spool, then acknowledges; the PDN advances its mark on that acknowledgement.
- **Crash records** have a radio sender that nothing uses. It broadcasts each pending `42`-byte record once per boot and advances the sent mark when the radio reports the broadcast transmitted. No FDN code receives, queues or acknowledges a crash record. The PDN build is instead wired to upload over Wi-Fi itself.
- **FDN uploads.** Match records upload on a five-minute idle timer. The FDN's crash upload runs once, at boot, although `fdn-apps` already asks for it on that timer as well.
- **Registration** runs between a PDN and the FDN it is Connected to, but every message travels over the radio in the clear: a 6-byte request carrying the PIN, and a 584-byte profile back. The PDN keeps nothing from registration across boots and registers again on every boot. The request message comes from the open change `pdn-pin-entry-fdn-registration`, which the code already follows; the base `player-provisioning` spec has no such message.
- **FDN lookups** use whatever scheme the configured server address has, and fall back to plain HTTP when it names none.
- **Serial jack**: carries only the 19-byte HELLO frame at 19200 baud. Its framing has an opcode byte, so a second frame type fits.
- **FDN beacon**: a broadcast every `12,000 ms`, but only while the FDN is on its idle or player-detected screen; it stops while a PDN is Connected. `fdn-apps` already asks for it whenever the radio is enabled. No PDN code listens for it.
- **Crypto in the PDN build**: HMAC-SHA-256 is available (mbedTLS message-digest layer, hardware SHA). ECDSA P-256 is compiled in; Ed25519 is not. The ESP32-S3 has no elliptic-curve accelerator.
- **Storage protection**: flash encryption, secure boot and NVS encryption are off in both builds. The SoC supports the eFuse-keyed HMAC peripheral, and nothing in the firmware uses an eFuse key block today.

## Goals / Non-Goals

**Goals:**

- A PDN that is never plugged in still delivers its crash records and match records.
- The server can tell that a record came from the holder of the record key issued for that MAC address, and that it was not altered on the way.
- A record is never retired on the PDN because a frame was merely sent.
- An FDN needs no secrets to relay.

**Non-Goals:**

- Authenticating FDNs to the server.
- Hiding record contents. Records travel in the clear as today.
- Proving a match happened. The tag proves who submitted a record, not that the opponent agrees. The server can compare the two PDNs' copies of one match ID.
- Proving a MAC address belongs to a particular physical PDN. A PDN has no identity from manufacturing; see "Enrolment" under Risks.
- Authenticating the FDN's beacon or its acknowledgement (see Risks).
- Removing PDN Wi-Fi itself. That is its own change; this one only stops depending on it.

## Decisions

### Tag scheme: per-PDN HMAC-SHA-256, truncated to 8 bytes

`tag = first 8 bytes of HMAC-SHA-256(record key, kind || PDN MAC || key ID || record bytes)`, with `kind` = `0x01` for a crash record and `0x02` for a match record. Every field has a fixed length for its kind, so the input is unambiguous. The record trailer is the key ID (1 byte) followed by the tag: `9` bytes per record.

Why 8 bytes. A guess can only be tested by submitting a record to the server, through an FDN or directly by someone on the network. Per attempt the chance of success is:

| Tag | Chance per forged record | A year of guessing at 1,000 per second | At 1,000,000 per second |
|---|---|---|---|
| 4 bytes | 2^-32 | certain; about 25 days on average | certain within hours |
| 8 bytes | 2^-64 | about 2 in a billion | about 2 in a million |
| 12 bytes | 2^-96 | negligible | negligible |
| 16 bytes | 2^-128 | negligible | negligible |

The rate is whatever the server allows. After a set number of failed verifications for a MAC address the server should stop verifying further records for it and quarantine them for an operator: a guesser gets a bounded number of tries, and a real PDN whose address is being abused has its records kept, not refused. With any such limit 8 bytes is far out of reach and 4 is not. RFC 2104 section 5 recommends a truncated HMAC be at least 80 bits and at least half the hash length (16 bytes here). That guidance covers uses where an attacker can collect many tags on messages of their choosing; here a PDN tags only its own records. If the owner prefers a length that needs no argument, 16 bytes costs 8 more bytes per record and nothing else in this design changes. The length is fixed once devices ship: changing it later is another wire change.

Tag length does not decide whether a record fits in a frame (see Frame size). Its cost is bytes in the FDN's storage and in each upload.

Alternatives considered:

- **Per-PDN ECDSA P-256 key pair.** Signature `64` bytes raw (up to `72` DER-encoded); public key `64` bytes. In the build today. Signing runs in software on this chip; time per record not measured.
- **Per-PDN Ed25519 key pair.** Signature `64` bytes; public key `32` bytes. Needs a library the build does not have.

The trade-off, stated plainly:

- **With HMAC, the server holds every PDN's record key.** A server breach lets the attacker forge records for every PDN, and recovery means an operator reset and a fresh registration for each one. The key also has to reach the PDN in secret: it crosses Wi-Fi inside TLS from the server to the FDN, sits in FDN memory for a moment, and crosses the serial jack.
- **With a key pair, the server holds only public keys and nothing on it can forge.** Nothing secret leaves the PDN, so registration needs no secret channel. The cost is 64 bytes per record instead of 8 in FDN storage and uploads, a software signature per record, and for Ed25519 a new library. Frame size does not rule it out: with a 64-byte signature in place of the 8-byte tag, a crash frame would be `113` bytes and a match frame `183`, against a `1469`-byte limit.
- **Neither fixes enrolment.** Either way the server has to decide which key belongs to a MAC address the first time it sees one.

This design follows the owner's decision for a short tag.

### The firmware-update key is not reused

Firmware images are signed with RSA-3072 and PDNs only verify (`firmware-distribution`, "Authenticity"). Every PDN can check that signature and none can produce one, and the release private key never goes on a device, so it cannot say which PDN wrote a record. Its signature block is also a 4096-byte sector, larger than a radio frame.

Where that key, or a second operations key handled the same way, could still help later:

- **Server-to-PDN data.** Anything the server wants PDNs to trust without a live connection (a signed configuration, a signed list of retired PDNs) could be signed once and verified by every PDN.
- **Certifying PDN keys.** If per-PDN key pairs are ever adopted, a certificate over each PDN's public key, signed at provisioning, would let the server accept a PDN it has never seen, which is the enrolment gap above.

Neither is part of this change. Today a PDN checks an update against the key that signed its running image through the platform's image verifier; using it for other data would need that public key exposed to the application.

### Key on the PDN

- **Issue.** The server issues the record key during registration. The PDN does not generate it.
- **Storage.** `32` bytes plus a 1-byte key ID in the PDN's non-volatile storage, in its own namespace, kept across boots. The player profile is discarded at every boot; the key cannot be, because crash records are offered before the PDN registers again.
- **Never replaced.** A PDN that holds a key ignores any key offered to it. Only an operator with the PDN in hand clears it (a USB command, like the existing crash-log dump). Otherwise anything that poses as an FDN on the jack could overwrite a good key and lock the PDN out.
- **Use.** Tags are computed when a record is sent, not when it is stored, so stored records need no extra bytes on the PDN.
- **Match records wait for the key.** A PDN with no key holds its match records and sends them, tagged, once it has one. Otherwise a PDN would send them untagged on the first beacon after boot, seconds before registration hands it a key. Crash records are sent with or without a key: a crash before the first registration is still worth having.
- **A stolen PDN, with flash encryption off.** Anyone with a USB cable can read the flash and find the record key. They can then forge records as that one PDN until the server retires its key. They learn nothing about any other PDN's key, and once PDN Wi-Fi is removed the image holds no network credential.
- **Optional hardening: eFuse-keyed storage.** The ESP32-S3 has an HMAC peripheral keyed from an eFuse block that software can use but not read, and the IDF in this build offers NVS encryption with its keys protected that way. With it, a flash dump no longer yields the record key. Limits: burning the eFuse is permanent and per-device, so it is a manufacturing decision; and without secure boot someone holding the PDN can flash their own firmware and read the key back through the peripheral, so it raises the bar from "read the flash" to "run code on it", no further. Availability is taken from the SoC capability flags and the IDF option in the build tree. It has not been built or tried on a board.

### Registration: the key crosses the jack, not the radio

1. The PDN's registration request (radio, as today) also reports its key ID, `0` if it has none.
2. The FDN's lookup names the Connected PDN's MAC address and that key ID.
3. The server returns the profile as today and, when this PDN needs a key, a 32-byte record key and its key ID.
4. The FDN sends the profile over the radio as today and the record key in one new framed message on the serial jack. It accepts a key only from a lookup made over HTTPS with the server's certificate verified (so it must refuse a server address that is not `https`), holds it in volatile memory only, never logs or stores it, and erases it as soon as the frame is written to the jack or the PDN disconnects.
5. The PDN accepts a record key only when it holds none, only from a jack on which an FDN is Connected, and only in the provisioning app while its own registration request to that FDN is outstanding. It discards a key that arrives damaged or malformed.

What the FDN forwards to the server is only the PDN's MAC address and reported key ID. Nothing secret travels upward.

There is no confirmation on the jack. If the key frame is lost, the PDN reports key ID `0` at its next registration and the server issues another (below); until then its match records wait. The jack already carries a HELLO frame every 20 ms from its own task, so the key frame has to be written without interleaving with one.

Why the jack: registration messages cross the radio unencrypted, so a key sent that way could be read by any radio nearby. The jack is the one link an eavesdropper cannot hear without touching the device. A 33-byte payload at 19200 baud takes about 20 ms. The serial framing belongs to the component that tracks jack connections; it should carry this as one opaque application frame, checked for length and CRC and handed up with the jack it arrived on, so the meaning of the bytes and the rules for accepting them stay with registration.

Server behaviour this design assumes (the server's to confirm):

- **Issue** a key when the PDN reports key ID `0` and the server holds no confirmed key for that MAC address. Record which player's PIN and which FDN asked.
- **Pending.** A key is pending until the first record verified under it. Several pending keys may exist for one MAC address (each lost delivery adds one), up to a small cap; all stay valid until one confirms, and then the rest are dropped. At the cap the server issues no more until one confirms or an operator resets the address.
- **Confirmed.** Once a key is confirmed, never send it again and never issue another for that MAC address without an operator reset. Without this rule anyone on the network could ask for any PDN's key.
- **Key IDs are never reused** for a MAC address, so a record tagged under a retired key cannot be mistaken for one under its successor. A MAC address that runs out of IDs needs an operator reset.
- **Alert** when a MAC address with a confirmed key reports key ID `0`: it has been wiped, or something else is using its address.
- **A PDN that reports a key ID the server does not hold** gets a profile and no key. Its records will fail verification until an operator clears the key on the PDN.

### Signed content and replay

- **Signed:** the record kind, the sending PDN's MAC address, the key ID, and the record bytes exactly as sent (`42` bytes for a crash record, `112` for a match record). The record number in the frame is transport bookkeeping and is not signed.
- **The record identity is inside the signed bytes:** the crash number for a crash record, the match ID for a match record. With the MAC address in the tag, a record cannot be moved to another PDN; with the kind byte, a crash tag cannot be passed off as a match tag.
- **Replay:** a replayed frame is byte-identical to one the server has. The server treats the same PDN and crash number, or the same PDN and match ID, with the same bytes, as one record, so a replay changes nothing. No counter or timestamp is needed on the device.
- **A consequence:** the two record layouts are now signed formats. Changing either means a new record kind, not an edit in place.
- **Crash numbers restart** if a PDN's storage is erased, so a later crash can reuse a number. "Same number, different bytes" is a different record; both the FDN and the server keep both.

### Frame size

One record frame serves both kinds: the transport's sequence ID, the record kind, a 4-byte record number, the record bytes, the trailer.

| Frame | Today | With this change | Limit |
|---|---|---|---|
| Crash record | `42` bytes | `57` bytes (1 + 1 + 4 + 42 + 9) | `1469` bytes |
| Match record | `116` bytes | `127` bytes (1 + 1 + 4 + 112 + 9) | `1469` bytes |

Both stay single frames with room to spare. The radio does not constrain the tag length.

### Relay: one frame, one acknowledgement, event-driven

- **One envelope.** Crash records and match records use the same record frame and the same stored acknowledgement (kind, number and record check). The FDN reads the kind from the frame, not from its length, so a later change to either record does not touch the intake.
- **Target.** The FDN that caused the send: the sender of the beacon just heard, or the FDN that just became Connected. Nothing is remembered between beacons, so there is no timer to tune, and a PDN plugged into an FDN that is full or busy still delivers to another one it can hear.
- **Triggers.** A beacon heard, an FDN becoming Connected, or a stored acknowledgement arriving. On the first two the PDN sends the lowest pending record of each kind; on the third, the next record of that kind. A record with no acknowledgement is simply sent again on the next beacon.
- **Kinds are independent.** A crash record stuck behind a full FDN queue does not hold back match records.
- **Transport.** Record frames go by the existing reliable unicast, which retries until the radio reports delivery or gives up. That covers a lost frame within a second or two; beacons cover everything longer. The match path moves onto it from its 100 ms re-send loop. Each kind gets its own packet type for the frame and for the acknowledgement, sharing one layout: the transport fixes one frame length per packet type, and a newer send to the same recipient on the same type replaces an older one that is still retrying.
- **In flight.** At most one record of each kind. A beacon that arrives while one is still retrying does nothing for that kind.
- **Acknowledgement.** An application message sent after the record is in non-volatile storage. It carries the kind, the number and a record check, the first 8 bytes of SHA-256 over the bytes and trailer the FDN stored, so an acknowledgement for some other frame under the same number does not count. Only an acknowledgement for the PDN's lowest pending record, from the FDN it was last sent to, moves the mark and releases the next record; anything else is ignored and a mark never moves backwards.
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

### Server check and responses

For each record the server looks up the keys for the named MAC address and key ID, recomputes the tag over the kind, MAC address, key ID and record bytes, and compares in constant time. It takes the record's contents from the signed bytes and rejects an upload whose decoded fields disagree with them.

| Case | Crash record (`POST /device-logs`) | Match record (`POST /quickdraw-matches`) |
|---|---|---|
| Tag verifies | accept | accept |
| Repeat of a stored record (same bytes) | accept, store nothing new | accept, store nothing new |
| Verified record with the same identity as an unauthenticated one | the verified record replaces it | the verified record replaces it |
| Bad tag, unknown key ID | `422`; keep the record in quarantine | per-entry rejection with a new error code; keep in quarantine |
| Key ID `0` for a MAC address that has a confirmed key | `422` | per-entry rejection |
| Key ID `0`, no confirmed key | accept, marked unauthenticated (see Open Questions) | per-entry rejection: a PDN never sends these |
| No signed bytes or trailer at all | treat as key ID `0` | per-entry rejection |
| More failed verifications for this MAC address than the limit | `422`; quarantine unverified | per-entry rejection; quarantine unverified |

An FDN's own crash records carry no trailer: an FDN has no record key, so they are unauthenticated like everything else an FDN says.

- **`422` matters** because the FDN stops its upload on an error and keeps the failed record. Without a "this will never succeed" answer one bad record would block the queue for good. Match uploads already retire a batch that comes back with per-entry rejections, and unknown error codes already map to a placeholder.
- **Quarantine matters** because by then the PDN has advanced its mark. A record rejected for its tag is gone from the devices; the server's copy is the only one an operator could recover after sorting out a key problem.
- **The decoded fields are transitional.** Today's uploads carry decoded fields; this change adds the signed bytes beside them so the current server shape keeps working. The server should treat the signed bytes as the only source and the fields as a check. Dropping the fields for relayed records is the cleaner end state and is the server team's call.

## Risks / Trade-offs

- **A fake FDN can swallow records.** Any device in range can broadcast a beacon and send stored acknowledgements; the PDN advances its mark and never resends. This is new: before, only a Connected FDN could acknowledge. The records stay in the PDN's ring buffers until overwritten and crash records can still be dumped over USB, but nothing rewinds a mark. → Needs the owner's sign-off (Open Questions).
- **Enrolment: the first claimant of a MAC address owns its key.** A MAC address is self-asserted on the jack and the radio. Someone with modified firmware who cables to an FDN under a victim's MAC address, with any valid PIN, before the victim has a confirmed key, is issued a key for it. At rollout no PDN has one. → The server records the player and FDN behind every issue and can flag a second MAC address for one player, or a keyed MAC address later reporting key ID `0`. It is attributable, not prevented. A hijacked address also leaves the real PDN without a key, so its match records wait unsent until an operator resets the address.
- **A stolen FDN reaches key issuance.** FDN flash is unencrypted and holds the Wi-Fi password, and the network has no device authentication. → Same limit as above: only MAC addresses without a confirmed key. Encrypting FDN flash is a separate decision.
- **A fake FDN on the jack can plant a key** in a PDN that has none. The server does not hold that key, so the PDN's records fail until an operator clears it. A PDN cannot tell a real FDN from a device that claims to be one.
- **Junk can keep an FDN's storage full.** Rejected junk is retired at each upload and can be refilled at once, so one radio can hold an FDN full for as long as it transmits while PDN ring buffers overwrite. Each acknowledgement to a new MAC address also takes a slot in the FDN's radio peer table, and each beacon from a new address takes one in the PDN's. → Bounded by capacity and the driver's eviction rule; not prevented.
- **Every PDN in range answers the same beacon.** Replies can collide; the transport retries and the next beacon is another chance.
- **A rejected record is lost to the devices.** The PDN advanced its mark when the FDN stored it. → Server quarantine, above.
- **A lost key needs an operator**, on the server and on the PDN. → The price of never replacing a key on request.
- **A PDN used only in demo mode never gets a key**, because the demo PIN never contacts the server. Its crash records stay untagged. A demo player's matches are not stored (`match-records-nil-player`), so no match records are held back.
- **The FDN handles each key once.** A compromised FDN firmware could copy keys as they pass.

## Migration Plan

1. **Server first.** Accept the new fields on both uploads and the lookup, issue keys, and verify tags.
2. **Firmware together.** Frame formats change, so every FDN and PDN takes the same build, as with the other wire changes in the comms rewrite.
3. **PDNs registered before keys existed** need nothing done to them. A PDN registers on every boot, so each one receives a key the first time it registers at an upgraded FDN. Match records it was already holding wait for that key and are sent tagged.
4. **The untagged window** applies to crash records only. The server accepts key ID `0` crash records from MAC addresses that have no confirmed key and marks them unauthenticated; that covers crashes before a PDN's first keyed registration. Match records need no window because a PDN never sends one untagged.
5. **Rollback.** The server can go back to ignoring the trailer at any time. Firmware rollback means reflashing every device.

## Open Questions

1. **Tag length.** 8 bytes is recommended. Confirm, or choose 16.
2. **Fake FDN.** Accept that a device posing as an FDN can make nearby PDNs retire records, or ask for a way to authenticate the acknowledgement (which needs the FDN to hold something the PDN can check)?
3. **Lost keys.** Operator reset on the server plus a clear on the PDN, as designed?
4. **Untagged crash records.** Accept them as unauthenticated from PDNs with no confirmed key for good (demo-only and never-registered PDNs have no other way to report), or stop at some date?
5. **Upload shape.** Signed bytes beside the decoded fields (this design), or signed bytes only? The server team's call.
6. **eFuse hardening.** A manufacturing decision; not needed for the rest of this change.
7. **Limits chosen here:** `32` relayed crash records on the FDN, and no relay during a match or shootout. Confirm or change.
8. **`Authorization: Bearer` wording.** Three of the requirements this change modifies said FDN requests are "authenticated HTTPS (`Authorization: Bearer <token>`)". The deltas drop that, following the decision that the network has no device authentication. Confirm.
