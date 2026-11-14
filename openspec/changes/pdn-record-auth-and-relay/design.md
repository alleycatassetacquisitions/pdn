## Context

Decided before this change and not reopened here:

- PDNs have no Wi-Fi. The network holds only FDNs and the server, with no device authentication between them; the server validates what it receives.
- Crash records and match records travel PDN -> FDN over the peer-to-peer radio, then FDN -> server over Wi-Fi. The PDN picks an FDN from its beacon (every `12,000 ms`), sends each record by reliable unicast, and advances its mark only when the FDN acknowledges having stored the record.
- Registration, symbol match and learning to play stay on the cable. A Connected FDN may still receive records; records never require the cable.
- A record is authenticated with a short tag, not a 64-byte signature. The firmware-update signing key is not reused.

Decided by the owner after a threat-model review of the first version of this proposal:

- Results decide prizes, and a few technical attendees may read out a PDN's flash. Flash encryption and secure boot are off, so a PDN's owner can read its key. A tag shows which PDN and player sent a record, not that the record is true.
- What catches a player lying about a result is comparing the two PDNs' copies of the same match. Tags make that comparison mean something over ESP-NOW, where a MAC address can be copied.
- A match counts on its first verified copy and is flagged if it is unconfirmed or contradicted.
- The record key is derived from a server secret, the PDN's MAC address and the player. Nothing is stored per PDN on the server.
- Only match records are tagged. Crash records go untagged and unverified, as every device log is today.
- The PDN holds its record key in volatile memory for the session only.
- An FDN limits PIN lookups itself, because a server-side limit may not get built.
- No flash encryption, no secure boot work, no eFuse key storage.
- The server team's capacity is unknown, so what this change asks of the server is a ranked list the team can cut from the bottom.
- The tag is 16 bytes. An FDN holds up to `32` relayed crash records. Nothing is relayed during a match or a shootout. The `Authorization: Bearer` wording is removed from the requirements this change touches.

What the code does today on `comms-rewrite`, the branch under the three open relay PRs:

- **Match records** go only to the FDN Connected on a jack. The PDN re-sends a `116`-byte frame (4-byte sequence number + `112`-byte match record) every 100 ms as a plain, unsequenced send until the FDN answers. The FDN checks the sender is Connected, writes the record to a 64-slot non-volatile spool, then acknowledges; the PDN advances its mark on that acknowledgement.
- **The match record** is `112` bytes: match UUID (16), hunter draw time (4), bounty draw time (4), hunter player UUID (16), bounty player UUID (16), hunter team UUID (16), bounty team UUID (16), winner player UUID (16), occurred-at in UTC milliseconds (8). The two PDNs in a match store every field identically except occurred-at, which each stamps from its own clock. A tie, a match in which neither side pressed, a shootout match and a match with a demo player are not stored.
- **Crash records** have a radio sender that nothing uses. It broadcasts each pending `42`-byte record once per boot and advances the sent mark when the radio reports the broadcast transmitted. No FDN code receives, queues or acknowledges a crash record. The PDN build is instead wired to upload over Wi-Fi itself.
- **FDN uploads.** Match records upload on a five-minute idle timer. The FDN's crash upload runs once, at boot, although `fdn-apps` already asks for it on that timer as well.
- **Registration** runs between a PDN and the FDN it is Connected to, but every message travels over the radio in the clear: a 6-byte request carrying the PIN, and a 584-byte profile back. The PDN keeps nothing from registration across boots and registers on every boot before it can play a match that is stored.
- **PIN lookups at the FDN** are not limited. The FDN makes one lookup at a time with no count, delay or lockout and tells the PDN hit or miss. It takes the PDN's MAC address from HELLO, which the PDN asserts itself and can change between attempts, and it has two input jacks and does not know which jack a request came from. A modified PDN on the cable can make an FDN try at least 7 PINs a minute, and hundreds when the server answers quickly.
- **FDN lookups** use whatever scheme the configured server address has, and fall back to plain HTTP when it names none.
- **Radio frames** are unencrypted and unauthenticated: peers are added without encryption, no primary key is set, and nothing above the driver encrypts.
- **Serial jack**: carries only the 19-byte HELLO frame at 19200 baud. Its framing has an opcode byte, so a second frame type fits.
- **FDN beacon**: a broadcast every `12,000 ms`, but only while the FDN is on its idle or player-detected screen; it stops while a PDN is Connected. `fdn-apps` already asks for it whenever the radio is enabled. No PDN code listens for it.
- **Crypto in the PDN build**: HMAC-SHA-256 is available (mbedTLS message-digest layer, hardware SHA). ECDSA P-256 is compiled in; Ed25519 is not. The ESP32-S3 has no elliptic-curve accelerator.
- **Storage protection**: flash encryption, secure boot and NVS encryption are off in both builds.
- **Network credentials**: the Wi-Fi name, the Wi-Fi password and the server address are compiled into every PDN image built so far.

What the three open PRs add on top. #426 replaces the FDN's match spool with one record store for both kinds (`32` crash records, `64` match records), accepts record frames from any sender, acknowledges after storing with the record check, and makes the beacon identify its sender as an FDN and run in every FDN state. #427 adds the PDN's relay component with its three triggers, the one-in-flight rule and the play gate, moves crash records onto it, and removes the crash broadcast sender and the PDN's boot-time Wi-Fi crash upload. #428 moves match records onto the relay and removes the 100 ms re-send loop. Their frames and FDN store entries still carry the `17`-byte trailer (key ID and tag, sent as zeros) that this revision removes. None of them uploads relayed crash records, changes an upload's fields, delivers a key or computes a tag.

What the server does today (alleycat-rs at `af107bf`; outside this repository):

- No device endpoint has authentication, key issuance or key storage.
- `POST /device-logs` takes `device_mac` as a field, so a relayed record under the PDN's MAC address is accepted today. A repeat of the same MAC address and crash number is ignored with `204`. A malformed body, invalid UTF-8 or a missing required field gets `400`; a body that is too large `413`; a wrong content type `415`. Nothing answers `422`.
- `POST /quickdraw-matches` rejects an unknown player or team per entry. A repeated match ID is stored once: the first upload wins and scores at once, and a second copy, even one naming a different winner, is dropped with `204`. The stored row records no uploader and no report count, and there is no view of disputed matches. A winner is required, so a tie cannot be stored. Occurred-at comes from the device and received-at from the server.
- ADR 0001 ("Quickdraw results and scoring periods") records that both devices upload the same duel, sometimes late and with slightly different timestamps; that there is one result per match ID and the first valid upload supplies the result and the timestamp; that a small timestamp difference near a scoring boundary can change eligibility depending on which device uploads first; and that the server does not keep both reports. It says to "revisit this if reconciliation becomes necessary."
- `GET /device/players/by-pin/{pin}` takes only the path parameter and answers `404` for an unknown PIN. A PIN is exactly 4 digits, so there are 10,000 values. There is no rate limit or lockout, and no way to block a device or a player.

## Goals / Non-Goals

**Goals:**

- A PDN that is never plugged in still delivers its crash records and match records.
- The server can tell that a match record came from a PDN registered to one of the two players in it, and that it was not altered on the way.
- The server can compare the two players' copies of a match and flag a match that is unconfirmed or contradicted.
- A record is never retired on the PDN because a frame was merely sent.
- An FDN needs no secrets to relay.
- Relayed crash records work against today's server.

**Non-Goals:**

- Proving a record is true. A PDN's owner can read its key, so a tag never proves that.
- Authenticating crash records.
- Authenticating FDNs to the server, the FDN's beacon or its acknowledgement (see Risks).
- Hiding record contents or the registration PIN. Both cross the radio in the clear as today.
- Flash encryption, secure boot or eFuse key storage.
- Removing PDN Wi-Fi itself. That is its own change; this one only stops depending on it.

## Decisions

### What a tag is for

A PDN's owner can read its flash and its memory, so its owner can make the PDN tag any record. The tag therefore answers one question: was this record sent by a PDN registered to this player. It does not answer whether the result is true.

A false result is caught by the other player's copy. The two PDNs in a match store the same match ID, players, teams, draw times and winner. If the server holds one copy tagged under each player's key and they agree, both players' PDNs reported the same result. If they disagree, someone changed one. If only one arrives, the match is unconfirmed.

Without tags that comparison is worth little over this radio. Frames are unauthenticated and a MAC address can be copied, so one person with a radio could send both copies. With a key derived from the MAC address and the player, a copy that verifies under a player's key came from a device that was handed that player's key at an FDN jack.

### Record key: derived, not stored

`record key = HMAC-SHA-256(server secret, PDN MAC address (6 bytes) || player UUID (16 bytes))`, 32 bytes. Both inputs have a fixed length, so the input is unambiguous.

- The server derives the key on demand and stores nothing per PDN (proposal to the server team; item 2 of the ranked list).
- Registering the same PDN to the same player again returns the same key. A different player on the same PDN, or the same player on another PDN, is a different key.
- The server can derive the key for any record from what the upload carries: the sender's MAC address and the two player UUIDs in the record bytes.
- The server secret is the one thing to protect. Whoever learns it can derive every PDN's key. Changing it invalidates every tag not yet verified; there is no per-PDN recovery step because there is nothing per PDN.

### Key on the PDN: volatile memory, for the session

- The PDN holds the 32-byte key in volatile memory and never writes it to non-volatile storage.
- This is enough because a PDN registers on every boot before it can play a match that is stored, and crash records need no key.
- The PDN never sends, logs or displays the key.
- A PDN's owner can still read the key from memory with a debugger or modified firmware. Holding it in volatile memory only removes the copy a flash dump of a powered-off PDN would find.
- A PDN whose player entered the demo PIN never contacts the server and holds no key. Its matches are not stored, so nothing goes untagged because of it.

### Registration: the key crosses the jack, not the radio

1. The PDN's registration request (radio, as today) is unchanged.
2. The FDN's lookup also names the Connected PDN's MAC address.
3. The server returns the profile as today and the 32-byte record key for that MAC address and player.
4. The FDN writes the key to the serial jack in one new framed message, and then sends the profile over the radio as today. It accepts a key only from a lookup made over HTTPS with the server's certificate verified (so it must refuse a server address that is not `https`), discards one that is not exactly 32 bytes, holds it in volatile memory only, never logs or stores it, and erases it as soon as the frame is written to the jack or the PDN disconnects.
5. The PDN accepts a key only from a jack on which an FDN is Connected, and only in the provisioning app after it has sent that FDN its PIN. It discards a key that arrives damaged or is not 32 bytes.

What the FDN forwards to the server is only the PDN's MAC address. Nothing secret travels upward.

There is no confirmation on the jack. If the key frame is lost, or the lookup response carries no key because the server does not derive keys yet, the PDN is registered and holds no key; its match records for that session carry the all-zero tag. The jack already carries a HELLO frame every 20 ms from its own task, so the key frame has to be written without interleaving with one.

Why the jack: registration messages cross the radio unencrypted, so a key sent that way could be read by any radio nearby. The jack is the one link an eavesdropper cannot hear without touching the device. A 32-byte payload at 19200 baud takes about 20 ms with its framing. The serial framing belongs to the component that tracks jack connections; it should carry this as one opaque application frame, checked for length and CRC and handed up with the jack it arrived on, so the meaning of the bytes and the rules for accepting them stay with registration.

### Tag: match records only, computed when the match ends

`tag = first 16 bytes of HMAC-SHA-256(record key, the 112 match record bytes)`.

- **Input.** The record bytes alone. The MAC address and the player are in the key, and only match records are tagged, so no kind byte, MAC address or key ID is needed in the input.
- **When.** The tag is computed when the match ends and stored with the record in the PDN's ring. A PDN registered to another player before the record is delivered therefore still delivers a valid record, tagged under the key of the player who played it.
- **No key.** A PDN that holds no key when a match ends stores the record with a 16-byte all-zero tag, which means "no tag". Match records are sent whether or not they carry a tag.
- **Storage.** A stored match record grows from `112` to `128` bytes on the PDN. A slot written by earlier firmware holds `112` bytes; it is read as carrying the all-zero tag, so no stored match is lost at the upgrade.
- **Crash records carry no tag.** A tag cannot make a crash record true, and the server already treats device logs as unverified.
- **Why 16 bytes.** A guess can only be tested by submitting a record to the server, and each guess succeeds with chance 2^-128. 16 bytes also meets RFC 2104 section 5 (at least 80 bits and at least half the hash length). The length is fixed once devices ship.
- **Replay.** A replayed frame is byte-identical to one the server has. Under the proposed server rules a second upload of the same copy changes nothing, so no counter is needed on the device.
- **A consequence.** The match record layout is now a signed format. Changing it means a new record kind, not an edit in place.

Alternatives considered and not adopted: a per-PDN ECDSA P-256 or Ed25519 key pair. The server would then hold only public keys. The cost is a 64-byte signature per record, a software signature on a chip with no elliptic-curve accelerator, a new library for Ed25519, and an enrolment step that decides which public key belongs to a MAC address. The private key would sit in the same unencrypted flash, so a PDN's owner could still sign anything.

### The firmware-update key is not reused

Firmware images are signed with RSA-3072 and PDNs only verify (`firmware-distribution`, "Authenticity"). Every PDN can check that signature and none can produce one, and the release private key never goes on a device, so it cannot say which PDN wrote a record. Its signature block is also a 4096-byte sector, larger than a radio frame.

### Frame size

One record frame serves both kinds: the transport's sequence ID, the record kind, a 4-byte record number, the record bytes and, for a match record, the tag.

| Frame | On `comms-rewrite` | Open PRs | This revision | Limit |
|---|---|---|---|---|
| Crash record | `42` bytes | `65` bytes (1 + 1 + 4 + 42 + 17) | `48` bytes (1 + 1 + 4 + 42) | `1469` bytes |
| Match record | `116` bytes | `135` bytes (1 + 1 + 4 + 112 + 17) | `134` bytes (1 + 1 + 4 + 112 + 16) | `1469` bytes |

Both stay single frames. An FDN's stored crash entry shrinks by the `17` trailer bytes and its stored match entry by the 1-byte key ID.

### Relay: one frame, one acknowledgement, event-driven

- **One envelope.** Crash records and match records use the same record frame and the same stored acknowledgement (kind, number and record check). The FDN reads the kind from the frame, not from its length, so a later change to either record does not touch the intake.
- **Target.** The FDN that caused the send: the sender of the beacon just heard, or the FDN that just became Connected. Nothing is remembered between beacons, so there is no timer to tune, and a PDN plugged into an FDN that is full or busy still delivers to another one it can hear.
- **Triggers.** A beacon heard, an FDN becoming Connected, or a stored acknowledgement arriving. On the first two the PDN sends the lowest pending record of each kind; on the third, the next record of that kind. A record with no acknowledgement is simply sent again on the next beacon.
- **Kinds are independent.** A crash record stuck behind a full FDN queue does not hold back match records.
- **Transport.** Record frames go by the existing reliable unicast, which retries until the radio reports delivery or gives up. That covers a lost frame within a second or two; beacons cover everything longer. The match path moves onto it from its 100 ms re-send loop. Each kind gets its own packet type for the frame and for the acknowledgement: the transport fixes one frame length per packet type, and a newer send to the same recipient on the same type replaces an older one that is still retrying.
- **In flight.** At most one record of each kind. A beacon that arrives while one is still retrying does nothing for that kind.
- **Acknowledgement.** An application message sent after the record is in non-volatile storage. It carries the kind, the number and a record check, the first 8 bytes of SHA-256 over the record bytes the FDN stored, followed by the tag for a match record. The PDN compares it with the frame as it sent it, so an acknowledgement for some other frame under the same number does not count. The check only identifies the stored frame; it is not keyed and proves nothing about who acknowledged. Only an acknowledgement for the PDN's lowest pending record, from the FDN it was last sent to, moves the mark and releases the next record; anything else is ignored and a mark never moves backwards.
- **Not during play.** Nothing is sent while a match or a shootout is active, and a send still retrying when one starts is cancelled, so record traffic cannot disturb reaction timing. An acknowledgement that arrives mid-match still moves the mark; the next record waits for the next beacon.
- **One owner on each side.** On the PDN, one relay component at the level of the game session owns the target, the triggers and the play gate; the crash log and the match store only answer "what is your next pending record" and "advance your mark". On the FDN, one record store generalised from the match spool holds both kinds, with a capacity per kind.

The gaps in the crash path on `comms-rewrite` and what closes each:

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

The FDN stores the sender's MAC address, the kind, the number, the record bytes and, for a match record, the tag, unchanged.

### FDN uploads

- **Crash records** upload in today's device-log shape: the originating PDN's MAC address and the decoded fields, with no new field. Today's server accepts this.
- **Retiring a relayed crash record.** The FDN retires it on success, and on `400`, `413`, `415` or `422`, each of which says the request itself can never be accepted; it then continues with the next record. On any other failure it stops and keeps the record for the next cycle. Without the retire rule one record the server will never take would block the queue for good. Crash numbers restart if a PDN's storage is erased; today's server then ignores the reused number with `204`, which also retires the record.
- **Match records** upload the decoded fields as today plus the sender's MAC address, the raw 112 record bytes and the 16-byte tag. The field format is the server team's. Firmware keeps the three new fields behind one marked seam in the upload encoder, and sends today's shape until the format is agreed.

### PIN attempts are limited at the FDN

After `5` lookups that the server answered "no such PIN" within `600,000 ms`, the FDN makes no lookup for `600,000 ms`. During that time it answers a request with the lookup-failure notification without contacting the server.

- **FDN-wide.** The FDN cannot tell cabled devices apart: the MAC address is self-asserted and the provisioning path does not know the jack. A limit per MAC address or per jack could be reset by changing the address.
- **A successful lookup does not reset the count.** Otherwise a guesser who knows one valid PIN could reset it at will.
- **Only "no such PIN" counts.** A timeout or a server error says nothing about a guess.
- **Volatile.** The count is in volatile memory, so restarting the FDN clears it.
- **The numbers are the editor's choice and need the owner's confirmation (Open Questions).** The arithmetic: the limit allows at most 5 guesses per 10 minutes sustained, which is 720 a day. A specific PIN takes 5,000 guesses on average out of 10,000, about 7 days. Some valid PIN among 200 players takes about 50 guesses on average (10,001 / 201), about 100 minutes. Today, at 7 guesses a minute, those figures are about 12 hours and about 7 minutes, and less when the server answers quickly.
- **The cost.** Five mistyped PINs at a busy FDN stop registration there for ten minutes.

This is new and supersedes nothing. It is built at the FDN because the server-side limit (item 4 of the ranked list) may not get built.

### Server: verification and match standing (proposal to the server team)

Everything in this section is a proposal to the server team. It describes the full behaviour; the ranked list below says which parts depend on which item.

- **Verification.** For a match upload the server takes the two player UUIDs from the raw bytes, derives the key for the sender's MAC address and each player, and checks the tag in constant time. If the tag matches under player P's key, the copy is verified as P's copy. That one check establishes both that the bytes are untouched and that the sender is a PDN registered to one of the two players.
- **Unverified.** A copy that verifies under neither key, including one with the all-zero tag, is kept as unverified. It never counts and never confirms.
- **Decoded fields.** The decoded fields in an upload must equal the fields in the raw bytes; an upload in which they differ is rejected per entry.
- **Both copies kept.** The server keeps each player's verified copy of a match.
- **Counts.** A match counts on its first verified copy and scores at once, as the first upload does today.
- **Confirmed.** A match is confirmed when the server holds a verified copy under each player's key and the two agree.
- **Unconfirmed.** A match is unconfirmed while the server holds a verified copy under only one player's key.
- **Contradicted.** A match is contradicted when the two verified copies disagree.
- **Flagged.** Unconfirmed and contradicted matches are flagged for a person to look at.
- **"Agree"** means every field of the 112 bytes is equal except occurred-at: match ID, both players, both teams, both draw times and winner. The firmware stores those identically on both PDNs, and occurred-at differs slightly by design (ADR 0001).
- **"The second copy"** means the copy verified under the other player's key, not a copy from another MAC address. Otherwise one player with two PDNs registered to themselves could supply both copies.
- **Scoring period.** The first verified copy's occurred-at decides the scoring period, as today. The server should also flag a match whose two copies fall in different scoring periods.
- **Crash records** need no change. They are stored as device logs under the originating PDN's MAC address and are unverified, as every device log is.

### Ranked server list (proposal to the server team)

The server team's capacity is unknown. The items are in rank order so the team can cut from the bottom. Firmware does not wait on any of them: crash relay works against today's server, and match records upload in today's shape until the new fields are agreed.

1. **Keep both copies of a match and flag contradicted and unconfirmed ones.** Without it, a lie by whoever uploads first stands silently, which is today's behaviour. Built without item 2, the server can only tell copies apart by a self-asserted MAC address, so a contradiction is still flagged but a confirmation can be faked by one radio.
2. **The lookup takes the PDN's MAC address and returns the derived key; match uploads carry the sender's MAC address, the raw bytes and the tag, and are verified.** Without it, the comparison in item 1 can be defeated by anyone with a radio who copies a MAC address, and firmware has no key to tag with, so PDNs send all-zero tags.
3. **Optional winner, for ties.** Without it a tie has no honest representation: the server requires a winner, so the firmware does not store a tied match today.
4. **Rate-limit the PIN lookup.** Without it the FDN-side limit is the only brake on PIN guessing.
5. **Block a PDN or a player.** Without it nothing can shut out a PDN or a player caught cheating.

### Superseded decisions

Each line is a decision of the earlier version of this proposal, what replaces it, and why.

1. **Every record tagged, crash records included** -> match records only. A tag cannot make a crash record true, the server already treats device logs as unverified, and relayed crash records then need no server change.
2. **A random key issued per registration and kept by the server, up to 255 per MAC address, named by a key ID** -> one key derived from a server secret, the PDN's MAC address and the player. There is nothing to store, nothing to wrap around and no key ID.
3. **A fresh key at every registration that replaces the stored one** -> the same PDN and player always get the same key. With a derived key there is nothing to rotate, and a lost key frame is repaired by registering again.
4. **Tag computed when the record is sent** -> computed when the match ends and stored with the record, so a PDN registered to another player still delivers a valid record.
5. **Key kept in the PDN's non-volatile storage across boots** -> volatile memory for the session. Registration happens on every boot before any match that is stored, and crash records need no key.
6. **A PDN without a key holds its match records** -> it sends them with the all-zero tag and the server keeps them as unverified. Holding them would lose every match on a PDN that never receives a key, for example while the server does not derive keys.
7. **Tag input of kind, MAC address, key ID and record bytes** -> the record bytes alone. The MAC address and the player are in the key, there is no key ID, and only one kind is tagged.
8. **Server keeps one record per match and a verified copy replaces an unverified one** -> the server keeps both players' copies and flags unconfirmed and contradicted matches. One record per match cannot show that the two players disagree.
9. **Only HTTP `422` retires a relayed crash record** -> `400`, `413`, `415` and `422` do. Today's server never answers `422`, and it answers the other three for a request it can never accept.
10. **Server-side operator actions "drop a MAC address's keys" and "alert on a burst of issues"** -> gone with stored keys. "Block" becomes item 5 of the ranked server list: block a PDN or a player.
11. **The accepted risk "anyone who registers a MAC address with a valid PIN is issued a working key"** -> restated as "a stolen PIN used to register a second PDN" (Risks), because the key now depends on the player as well as the MAC address.

The record trailer and the "unverified crash record" and "quarantine" server rules go with decisions 1, 2 and 8.

## Risks / Trade-offs

Stated plainly first:

- A PDN's owner can read its key, so a tag never proves a record is true.
- ESP-NOW frames are unencrypted, so records and the registration PIN travel in the clear.
- A 4-digit PIN can be guessed. The FDN limit only slows it.

Accepted by the owner:

- **Two players colluding.** Both copies agree on a false result and the match shows as confirmed. Nothing in this design detects it.
- **A stolen PIN used to register a second PDN.** The second PDN gets a valid key for that player. With the opponent's PIN a cheat can supply "the opponent's copy" and confirm a false result alone. PINs cross the radio in the clear at registration, so anyone listening near an FDN can collect them. → Item 5 of the server list is the only response once it is noticed.
- **A fake FDN swallowing records.** A device that beacons and acknowledges makes PDNs retire records. The matches then show as unconfirmed or missing on the server. The records stay in the PDN's ring buffers until overwritten, but nothing rewinds a mark.
- **A loser withholding their copy.** The match counts on the winner's copy and shows as unconfirmed. A player who keeps their PDN away from FDNs, or wipes it, produces the same result as an honest player whose PDN broke.

Other risks:

- **The server secret.** Whoever learns it can derive every key. → The server team owns its protection; nothing per PDN needs recovery if it is changed, but every tag not yet verified stops verifying.
- **A fake FDN on the jack can hand a PDN a wrong key.** The PDN's matches for that session then arrive as unverified. A PDN cannot tell a real FDN from a device that claims to be one.
- **A PDN that never receives a key sends all-zero tags.** Its matches are kept as unverified and never count once the server verifies. → This is why the server items are ranked: until item 2 is built every PDN is in this state and the server must keep counting as it does today.
- **The PIN limit can be used to stop registration at an FDN.** Five wrong PINs from one cabled PDN, or from a radio that copies a Connected PDN's MAC address, stop lookups there for `600,000 ms`, and can be repeated. → Accepted as the cost of the limit; the numbers are for the owner to confirm.
- **Restarting an FDN clears the PIN count.** Someone who can cycle an FDN's power gets 5 more guesses per restart.
- **Unverified crash records can be created under any MAC address** by anyone in radio range. This is true of every device log today; the server must keep crash records out of anything that counts or alerts per PDN.
- **Junk can keep an FDN's storage full.** Junk leaves the FDN at each upload and can be refilled at once, so one radio can hold an FDN full for as long as it transmits while PDN ring buffers overwrite. Each acknowledgement to a new MAC address also takes a slot in the FDN's radio peer table, and each beacon from a new address takes one in the PDN's. → Bounded on the devices by capacity and the driver's eviction rule.
- **Every PDN in range answers the same beacon.** Replies can collide; the transport retries and the next beacon is another chance.
- **The FDN handles a key at every registration.** A compromised FDN firmware could copy keys as they pass.
- **The venue network.** Every PDN image built so far carries the Wi-Fi password, so until it is changed anyone holding a PDN can join the network and reach the server's endpoints, none of which authenticate (Migration Plan).

## Migration Plan

1. **Rework the open PRs.** #426, #427 and #428 lose the key ID and the crash trailer from their frames, the record check and the FDN store entries before they merge, so the `17`-byte trailer never ships.
2. **Firmware together.** Frame formats change, so every FDN and PDN takes the same build, as with the other wire changes in the comms rewrite. Crash relay and match relay work against today's server.
3. **Stored match records.** A PDN's match slots written by earlier firmware are read as carrying the all-zero tag.
4. **Server items in rank order** (proposals to the server team): keep both copies and flag; derived key on the lookup and verified match uploads; optional winner; lookup rate limit; block a PDN or a player. When item 2 is agreed, firmware fills the upload seam and the lookup starts naming the MAC address in the agreed format.
5. **No cut-over for keys.** A PDN registers on every boot, so each one receives its key the first time it registers at an FDN after the server starts deriving keys. Match records stored before that carry the all-zero tag and are kept as unverified.
6. **Rollback.** The server can stop verifying at any time and count the first upload as today. Firmware rollback means reflashing every device.
7. **Wi-Fi password.** The venue Wi-Fi password must be changed once PDN Wi-Fi is removed (its own change), because every PDN flashed so far carries it; changing it means rebuilding every FDN. Until then anyone holding a PDN can join the network.

## Open Questions

For the owner to confirm:

1. **The PIN limit numbers:** `5` "no such PIN" answers within `600,000 ms`, then no lookups for `600,000 ms`, FDN-wide. They are the editor's choice.
2. **Session-only key and untagged match records:** the PDN holds its key in volatile memory for the session only, and a PDN without a key sends its match records with the all-zero tag instead of holding them.
