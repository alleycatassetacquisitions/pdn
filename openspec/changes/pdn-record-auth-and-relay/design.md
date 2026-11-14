## Context

Decided before the threat-model review and not reopened here:

- PDNs have no Wi-Fi. The network holds only FDNs and the server, with no device authentication between them; the server validates what it receives.
- Crash records and match records travel PDN -> FDN over the peer-to-peer radio, then FDN -> server over Wi-Fi. The PDN picks an FDN from its beacon (every `12,000 ms`), sends each record by reliable unicast, and advances its mark only when the FDN acknowledges having stored the record.
- Registration, symbol match and learning to play stay on the cable. A Connected FDN may still receive records; records never require the cable.
- A match record is authenticated with a short tag, not a 64-byte signature. The firmware-update signing key is not reused.
- The tag is 16 bytes.
- An FDN holds up to `32` relayed crash records.
- Nothing is relayed during a match or a shootout.
- The `Authorization: Bearer` wording is removed from the requirements this change touches.

Decided by the owner after the threat-model review:

- Results decide prizes, and a few technical attendees may read out a PDN's flash. Flash encryption and secure boot are off.
- What catches a player lying about a result is comparing the two PDNs' copies of the same match. Tags make that comparison mean something over a radio on which a MAC address can be copied.
- A match counts on its first verified copy and is flagged if it is unconfirmed or contradicted.
- The record key is derived from a server secret, the PDN's MAC address and the player. Nothing is stored per PDN on the server.
- Only match records are tagged. Crash records go untagged and unverified, as every device log is today.
- No flash encryption, no secure boot work, no eFuse key storage.
- The server team's capacity is unknown, so what this change asks of the server is a ranked list the team can cut from the bottom.

The editor's choices, each waiting on the owner (Open Questions): the record key held in volatile memory only; a PDN with no key sending match records with the all-zero tag; the FDN's delay after an unknown PIN and its numbers.

What the code does today on `comms-rewrite`, the branch under the three open relay PRs:

- **Match records** go only to the FDN Connected on a jack. The PDN re-sends a `116`-byte frame (4-byte sequence number + `112`-byte match record) every 100 ms as a plain, unsequenced send until the FDN answers. The FDN checks the sender is Connected, writes the record to a 64-slot non-volatile spool, then acknowledges; the PDN advances its mark on that acknowledgement.
- **The match record** is `112` bytes: match UUID (16), hunter draw time (4), bounty draw time (4), hunter player UUID (16), bounty player UUID (16), hunter team UUID (16), bounty team UUID (16), winner player UUID (16), occurred-at in UTC milliseconds (8). The two PDNs in a match store every field identically except occurred-at, which each stamps from its own clock.
- **Matches that are not stored**: a tie, a match in which neither side pressed, a shootout match, a match with a demo player, and a match on a PDN that never received the opponent's result. The last means honest play can leave one PDN with a record the other does not have.
- **Crash records** have a radio sender that nothing uses. It broadcasts each pending `42`-byte record once per boot and advances the sent mark when the radio reports the broadcast transmitted. No FDN code receives, queues or acknowledges a crash record. The PDN build is instead wired to upload over Wi-Fi itself.
- **Core dumps**: the build writes a core dump to flash on a crash.
- **FDN uploads.** Match records upload on a five-minute idle timer. The FDN's crash upload runs once, at boot, although `fdn-apps` already asks for it on that timer as well.
- **Registration** runs between a PDN and the FDN it is Connected to, but every message travels over the radio in the clear: a 6-byte request carrying the PIN, and a 584-byte profile back. Anyone near an FDN while a player registers learns that player's PIN. The PDN keeps nothing from registration across boots and registers on every boot before it can play a match that is stored.
- **PIN requests** are accepted by the FDN from any radio source whose MAC address equals a Connected PDN's. A radio that copies a plugged-in PDN's MAC address can send PIN requests with no cable.
- **PIN lookups at the FDN** are not limited. The FDN makes one lookup at a time with no count, delay or lockout and tells the PDN hit or miss. It takes the PDN's MAC address from HELLO, which the PDN asserts itself and can change between attempts, and it has two input jacks and does not know which jack a request came from. A modified PDN on the cable can make an FDN try at least 7 PINs a minute, and hundreds when the server answers quickly.
- **FDN lookups** use whatever scheme the configured server address has, and fall back to plain HTTP when it names none.
- **Radio frames** are unencrypted and unauthenticated: ESP-NOW peers are added without encryption, no primary key is set, and nothing above the driver encrypts.
- **Serial jack**: carries only the 19-byte HELLO frame at 19200 baud. Its framing has an opcode byte, so a second frame type fits.
- **FDN beacon**: a broadcast every `12,000 ms`, but only while the FDN is on its idle or player-detected screen; it stops while a PDN is Connected. `fdn-apps` already asks for it whenever the radio is enabled. No PDN code listens for it.
- **Crypto in the PDN build**: HMAC-SHA-256 is available (mbedTLS message-digest layer, hardware SHA). ECDSA P-256 is compiled in; Ed25519 is not. The ESP32-S3 has no elliptic-curve accelerator.
- **Storage protection**: flash encryption, secure boot and NVS encryption are off in both builds.
- **Network credentials**: the Wi-Fi name, the Wi-Fi password and the server address are compiled into every PDN image built so far.

What the three open PRs add on top. #426 replaces the FDN's match spool with one record store for both kinds (`32` crash records, `64` match records), accepts record frames from any sender, acknowledges after storing with the record check, and makes the beacon identify its sender as an FDN and run in every FDN state. #427 adds the PDN's relay component with its three triggers, the one-in-flight rule and the play gate, moves crash records onto it, and removes the crash broadcast sender and the PDN's boot-time Wi-Fi crash upload. #428 moves match records onto the relay and removes the 100 ms re-send loop. Their frames and FDN record store entries still carry the `17`-byte trailer (key ID and tag, sent as zeros) that this revision removes. None of them uploads relayed crash records, changes an upload's fields, delivers a key or computes a tag.

#426 also adds a portable SHA-256 to the shared library, used for the record check.

What the server does today (alleycat-rs at `af107bf`; outside this repository):

- Nothing authenticates a caller. No endpoint has authentication, key issuance or key storage.
- Every player endpoint that returns a PIN (`GET /players`, `GET /players/{id}`, `GET /players/by-pin/{pin}`) returns it with the player's name and email, to any caller. Player UUIDs cross the radio in the clear, so one UUID yields that player's PIN.
- `POST /device-logs` takes `device_mac` as a field, so a relayed record under the PDN's MAC address is accepted today. A repeat of the same MAC address and crash number is ignored with `204`. A malformed body, invalid UTF-8 or a missing required field gets `400`; a body that is too large `413`; a wrong content type `415`. Nothing answers `422`.
- `POST /quickdraw-matches` accepts a match from any caller and rejects an unknown player or team per entry. A repeated match ID is stored once: the first upload wins and scores at once, and a second copy, even one naming a different winner, is dropped with `204`. The stored row records no uploader and no report count, and there is no view of disputed matches. A winner is required, so a tie cannot be stored. Occurred-at comes from the device and received-at from the server.
- ADR 0001 ("Quickdraw results and scoring periods") records that both devices upload the same duel, sometimes late and with slightly different timestamps; that there is one result per match ID and the first valid upload supplies the result and the timestamp; that a small timestamp difference near a scoring boundary can change eligibility depending on which device uploads first; and that the server does not keep both reports. It says to "revisit this if reconciliation becomes necessary."
- `GET /device/players/by-pin/{pin}` takes only the path parameter. It answers `404` for an unknown PIN and also when no active event is selected. A PIN is exactly 4 digits, so there are 10,000 values. There is no rate limit or lockout, and no way to block a device or a player.

## Goals / Non-Goals

**Goals:**

- A PDN that is never plugged in still delivers its crash records and match records.
- The server can tell that a match record's tag was made with the key it derives for the sender's MAC address and one of the two players in the record, and that the record was not altered after it was tagged.
- The server can compare the two players' copies of a match and flag a match that is unconfirmed or contradicted.
- A record is never retired on the PDN because a frame was merely sent.
- An FDN needs no secrets to relay.
- Relayed crash records work against today's server.

**Non-Goals:**

- Proving a record is true.
- Authenticating crash records.
- Authenticating FDNs to the server, the FDN's beacon or its acknowledgement (see Risks).
- Authenticating callers of the server. The design depends on who can reach it (Risks, first entry).
- Hiding record contents or the registration PIN. Both cross the radio in the clear as today.
- Flash encryption, secure boot or eFuse key storage.
- Removing PDN Wi-Fi itself. That is its own change; this one only stops depending on it.

## Decisions

### What a tag is for

A PDN's owner can read its key, so a tag never proves a record is true: the owner can make the PDN tag any record. A tag that verifies says one thing: the tag was made with the key the server derives for that MAC address and player.

A false result is caught by the other player's copy. The two PDNs in a match store the same match ID, players, teams, draw times and winner. If the server holds one copy tagged under each player's key and they agree, the result was reported under both players' keys. If they disagree, one was changed. If only one arrives, the match is unconfirmed.

Without tags that comparison is worth little over this radio. Frames are unauthenticated and a MAC address can be copied, so one person with a radio could send both copies.

The comparison is only as strong as the path by which a key is handed out. A key reaches whoever presents the player's PIN at an FDN, and whoever can call the lookup directly (Risks).

### Record key: derived, not stored

`record key = HMAC-SHA-256(server secret, PDN MAC address (6 bytes) || player UUID (16 bytes))`, 32 bytes. Both inputs have a fixed length, so the input is unambiguous.

- The server derives the key on demand and stores nothing per PDN (proposal to the server team; item 2 of the ranked list).
- Registering the same PDN to the same player again returns the same key. A different player on the same PDN, or the same player on another PDN, is a different key.
- The server can derive the key for any record from what the upload carries: the sender's MAC address and the two player UUIDs in the record bytes.
- **The key is permanent.** It does not depend on the PIN, so changing a stolen PIN does not revoke a key already handed out.
- **The MAC address is self-asserted.** The FDN takes it from HELLO. A cheat who has the opponent's PIN and claims the opponent's MAC address receives the opponent's actual key.
- **Only changing the server secret revokes a key, and it revokes all of them.** Every PDN's key is then wrong until that PDN restarts and registers again, and every tag made before the change stops verifying.
- **Option for the server team, not part of this design:** a per-player version number in the derivation input. Raising one player's number would revoke that player's keys alone. It costs one stored number per player.

### Key on the PDN: volatile memory only (editor's choice; Open Questions)

- The PDN holds the 32-byte key in volatile memory and never writes it to the storage it keeps deliberately: its key-value storage and its match ring.
- The PDN sends the key to no device and never logs or displays it.
- A key accepted later in the same session replaces the held one.
- The PDN drops the key when it leaves registration without a profile applied, so a key never outlasts a registration that failed.
- **Exception: core dumps.** The build writes a core dump to flash on a crash, and a core dump can contain the key if a copy is on a task stack or in a dumped region. The firmware wipes working copies after use and keeps the key off task stacks where practical (tasks.md 5.7). That narrows the exception and does not close it.
- A PDN whose player entered the demo PIN never contacts the server and holds no key. Its matches are not stored.

### Registration: the key crosses the jack, not the radio

1. The PDN's registration request (radio, as today) is unchanged.
2. The FDN's lookup also names the Connected PDN's MAC address.
3. The server returns the profile as today and the 32-byte record key for that MAC address and player.
4. The FDN writes the key to the serial jack in one new framed message, and then sends the profile over the radio as today. It accepts no record key from a lookup that was not made over HTTPS with the certificate verified, discards one that is not exactly 32 bytes, holds it in volatile memory only, never logs or stores it, and erases it as soon as the frame is written to the jack or the PDN disconnects.
5. The PDN accepts a key only from a jack on which an FDN is Connected, and only in the provisioning app after it has sent that FDN its PIN. It discards a key whose frame fails its integrity check or that is not 32 bytes.

What the FDN forwards to the server is only the PDN's MAC address. Nothing secret travels upward.

There is no confirmation on the jack and the PDN shows nothing about its key. If the key frame is lost, or the lookup response carries no key, the PDN is registered and holds no key; its match records for that session carry the all-zero tag (Risks). The jack already carries a HELLO frame every 20 ms from its own task, so the key frame has to be written without interleaving with one.

Why the jack: registration messages cross the radio unencrypted, so a key sent that way could be read by any radio nearby. The jack is the one link an eavesdropper cannot hear without touching the device. A 32-byte payload at 19200 baud takes about 20 ms with its framing. The serial framing belongs to the component that tracks jack connections; it should carry this as one opaque application frame, checked for length and CRC and handed up with the jack it arrived on, so the meaning of the bytes and the rules for accepting them stay with registration.

### Tag: match records only, computed when the match ends

`tag = first 16 bytes of HMAC-SHA-256(record key, the 112 match record bytes)`.

- **Input.** The record bytes alone.
- **When.** The tag is computed when the match ends and stored with the record in the PDN's ring.
- **No key (editor's choice; Open Questions).** A PDN that holds no key when a match ends stores the record with a 16-byte all-zero tag, which means "no tag". Match records are sent whether or not they carry a tag.
- **Storage.** A stored match record grows from `112` to `128` bytes on the PDN. A slot holding `112` bytes and no tag is read as carrying the all-zero tag. Such a record is still delivered; it stops counting once the server verifies (Migration Plan).
- **Crash records carry no tag.**
- **Implementation.** HMAC-SHA-256 is built on the portable SHA-256 in the shared library, so native tests and the PDN build run the same code. One fixed test vector is in `record-authentication` and is shared with the server team.
- **Why 16 bytes.** A guess can only be tested by submitting a record to the server, and each guess succeeds with chance 2^-128. 16 bytes also meets RFC 2104 section 5 (at least 80 bits and at least half the hash length). The length is fixed once devices ship.
- **Replay.** A replayed frame is byte-identical to one the server has. Under the proposed server rules a second upload of the same copy changes nothing, so no counter is needed on the device.
- **A consequence.** The match record layout is now a signed format. Changing it means a new record kind, not an edit in place.

Alternatives considered and not adopted: a per-PDN ECDSA P-256 or Ed25519 key pair. The server would then hold only public keys. The cost is a 64-byte signature per record, a software signature on a chip with no elliptic-curve accelerator, a new library for Ed25519, and an enrolment step that decides which public key belongs to a MAC address. The private key would sit in the same unencrypted flash.

### The firmware-update key is not reused

Firmware images are signed with RSA-3072 and PDNs only verify (`firmware-distribution`, "Authenticity"). Every PDN can check that signature and none can produce one, and the release private key never goes on a device, so it cannot say which PDN wrote a record. Its signature block is also a 4096-byte sector, larger than a radio frame.

### Frame size

One record frame serves both kinds: the transport's sequence ID, the record kind, a 4-byte record number, the record bytes and, for a match record, the tag.

| Frame | On `comms-rewrite` | Open PRs | This revision | Limit |
|---|---|---|---|---|
| Crash record | `42` bytes | `65` bytes (1 + 1 + 4 + 42 + 17) | `48` bytes (1 + 1 + 4 + 42) | `1469` bytes |
| Match record | `116` bytes | `135` bytes (1 + 1 + 4 + 112 + 17) | `134` bytes (1 + 1 + 4 + 112 + 16) | `1469` bytes |

Both stay single frames. The stored acknowledgement is `14` bytes (1 + 1 + 4 + 8). An FDN's stored crash entry shrinks by the `17` trailer bytes and its stored match entry by the 1-byte key ID.

### Relay: one frame, one acknowledgement, event-driven

- **One envelope.** Crash records and match records use the same record frame and the same stored acknowledgement (kind, number and record check). The FDN reads the kind from the frame, not from its length, so a later change to either record does not touch the intake.
- **Target.** The FDN that caused the send: the sender of the beacon just heard, or the FDN that just became Connected. Nothing is remembered between beacons, so there is no timer to tune, and a PDN plugged into an FDN that is full or busy still delivers to another one it can hear.
- **Triggers.** A beacon heard, an FDN becoming Connected, or a stored acknowledgement arriving. On the first two the PDN sends the lowest pending record of each kind; on the third, the next record of that kind. A record with no acknowledgement is simply sent again on the next beacon.
- **Kinds are independent.** A crash record stuck behind a full FDN record store does not hold back match records.
- **Transport.** Record frames go by the existing reliable unicast, which retries until the radio reports delivery or gives up. That covers a lost frame within a second or two; beacons cover everything longer. The match path moves onto it from its 100 ms re-send loop. Each kind gets its own packet type for the frame and for the acknowledgement: the transport fixes one frame length per packet type, and a newer send to the same recipient on the same type replaces an older one that is still retrying.
- **In flight.** At most one record of each kind. A beacon that arrives while one is still retrying does nothing for that kind.
- **Acknowledgement.** An application message sent after the record is in the FDN's record store. It carries the kind, the number and a record check: the first 8 bytes of SHA-256 over the record bytes and, for a match record, the tag appended to them. The PDN compares it with the frame as it sent it, so an acknowledgement for some other frame under the same number does not count. 8 bytes are enough because the check only tells stored frames apart; it is not keyed, is not a security bound, and proves nothing about who acknowledged. Only an acknowledgement for the PDN's lowest pending record, from the FDN it was last sent to, moves the mark and releases the next record; anything else is ignored and a mark never moves backwards.
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

The FDN's record store keeps the sender's MAC address, the kind, the number, the record bytes and, for a match record, the tag, unchanged.

Whether an FDN takes match records from a PDN that is not Connected before the server verifies tags is an open question for the owner (Open Questions, 2). The specs here describe acceptance from any PDN.

### FDN uploads

- **When.** Crash records upload at boot and on the periodic uplink. The FDN's own records go first, then the relayed ones. The two groups are independent: a failure on one of the FDN's own records stops its own records for that cycle and does not stop the relayed ones.
- **Crash records** upload in today's device-log shape: the originating PDN's MAC address and the decoded fields, with no new field. Today's server accepts this.
- **Retiring a relayed crash record.** The FDN retires it on success, and on `400`, `413`, `415` or `422`, each of which says the request itself can never be accepted; it then continues with the next relayed record. On any other failure it stops the relayed records and keeps them for the next cycle. Without the retire rule one record the server will never take would block the record store for good. The rule does not apply to the FDN's own crash records, which advance only on success as today. Crash numbers restart if a PDN's storage is erased; today's server then ignores the reused number with `204`, which also retires the record.
- **Match records** upload the decoded fields as today plus the sender's MAC address, the raw 112 record bytes and the 16-byte tag. The field format is the server team's.
- **The seam.** Firmware keeps the three new upload fields, and the MAC address on the lookup, behind one marked seam each, and sends today's shapes until the server team agrees the formats. These describe the target and are met only when the seams are filled (tasks.md 10.3), not when the firmware tasks before that finish: the scenario "Uploaded match carries the sender, the record bytes and the tag alongside the decoded fields" (`match-collection`); the scenario "Lookup names the Connected PDN" (`player-provisioning`); and, in `record-relay`, the upload sentence of the requirement "FDN relays without verifying" with the upload part of its scenarios "A record with a wrong tag is still relayed" and "A match record with the all-zero tag is still relayed".

### FDN delay after an unknown PIN (editor's choice; Open Questions)

After a lookup the server answers with HTTP `404`, the FDN makes no lookup for a delay. The delay is `2,000 ms` after the first `404` and doubles with each further `404`, up to `120,000 ms`. It returns to none after `600,000 ms` without a `404`. While a delay runs the FDN answers a PIN request with the lookup-failure notification without contacting the server, and its display says `"LOOKUPS"` / `"PAUSED"`. A successful lookup changes nothing. The state is FDN-wide and in volatile memory.

- **Why FDN-wide.** The PIN request arrives over the radio under a self-asserted MAC address, so the FDN cannot count per device.
- **Why a delay and not a stop.** A radio that copies a Connected PDN's MAC address can send PIN requests with no cable, so it can trigger the limit. A stop would then block every registration at that FDN for its whole length. A delay bounds one wait at `120,000 ms`. It does not stop a persistent radio: one that sends a wrong PIN each time a delay ends takes the FDN's single lookup and starts another `120,000 ms`, so registration at that FDN is blocked for as long as it transmits. The same radio can block registration today as well, by keeping the one lookup busy.
- **Why at the FDN.** The server-side limit (item 4 of the ranked list) may not get built.
- **Arithmetic, per FDN.** The best sustained rate is 7 lookups per 726 s: `404` answers at 0, 2, 6, 14, 30, 62 and 126 s, then 600 s idle, after which the delay is none again. That is about 104 s per lookup, about 833 a day. A specific PIN takes 5,000 guesses on average out of 10,000, about 6 days. Some valid PIN among 200 players takes about 50 guesses on average (10,001 / 201), about 86 minutes. Today, at 7 guesses a minute, those figures are about 12 hours and about 7 minutes.
- **What multiplies the rate.** k FDNs give k times the rate. Restarting an FDN clears its delay.
- **A server with no active event also triggers the delay**, because the lookup answers `404` in that case too.
- **An attacker often need not guess at all.** The PIN crosses the radio in the clear at every registration, and the player endpoints return PINs to anyone who can reach the server.

### Server: verification and match standing (proposal to the server team)

Everything in this section is a proposal to the server team. Each rule names the item of the ranked list it belongs to.

- **Verification (item 2).** For a match upload the server takes the two player UUIDs from the raw bytes, derives the key for the sender's MAC address and each player, and checks the tag in constant time. If the tag matches under player P's key, the copy is verified as P's copy. That one check establishes that the bytes are unchanged since they were tagged and that the tag was made with the key the server derives for that MAC address and player P.
- **Unverified copy (item 2).** A copy that verifies under neither key, including one with the all-zero tag, is kept as unverified. It never counts and never confirms.
- **Decoded fields (item 2).** The decoded fields in an upload must equal the fields in the raw bytes; an upload in which they differ is rejected per entry.
- **First copy per player kept (items 1 and 2).** The server keeps the first verified copy per match and player. A later, different copy verified under the same player's key never replaces it, and flags the match.
- **Counts (item 2).** A match counts on its first verified copy and scores at once, as the first upload does today.
- **Standing (items 1 and 2),** computed from the first verified copy per player:
  - *Confirmed*: a verified copy under each player's key, and the two agree.
  - *Unconfirmed*: a verified copy under only one player's key.
  - *Contradicted*: a verified copy under each player's key, and the two disagree.
  - *Unverified*: only unverified copies. The match does not count.
- **Flagged (item 1).** Unconfirmed, contradicted and unverified matches are flagged for a person to look at.
- **"Agree"** means every field of the 112 bytes is equal except occurred-at: match ID, both players, both teams, both draw times and winner. The firmware stores those identically on both PDNs, and occurred-at differs slightly by design (ADR 0001).
- **"The second copy"** means the copy verified under the other player's key, not a copy from another MAC address. Otherwise one player with two PDNs registered to themselves could supply both copies.
- **Scoring period (item 1).** The first verified copy's occurred-at decides the scoring period, as today. The server should also flag a match whose two copies fall in different scoring periods.
- **Until verification is turned on,** the server counts the first copy as it does today, and copies with the all-zero tag count (Migration Plan).
- **Crash records** need no change. They are stored as device logs under the originating PDN's MAC address and are unverified, as every device log is. Proposal to the server team: keep crash records out of anything that counts or alerts per PDN.

### Ranked server list (proposal to the server team)

The server team's capacity is unknown. The items are in rank order so the team can cut from the bottom. Firmware does not wait on any of them: crash relay works against today's server, and match records upload in today's shape until the new fields are agreed.

**Precondition, unranked (the editor's addition, not the owner's):** every player endpoint that returns a PIN (`GET /players`, `GET /players/{id}`, `GET /players/by-pin/{pin}`), the device lookup, and `POST /quickdraw-matches` must not be reachable by players. Without that, items 1 and 2 protect nothing against anyone on the network.

1. **Keep both copies of a match and flag contradicted and unconfirmed ones.** Without it, a lie by whoever uploads first stands silently, which is today's behaviour. Built without item 2, the upload has no sender at all. The server can then flag two different copies of a match ID, but it cannot tell the other player's copy from the same copy sent twice, and it cannot flag a match as unconfirmed.
2. **The lookup takes the PDN's MAC address and returns the derived key; match uploads carry the sender's MAC address, the raw bytes and the tag, and are verified.** Without it, the comparison in item 1 can be defeated by anyone with a radio who copies a MAC address, and firmware has no key to tag with, so PDNs send all-zero tags.
3. **Optional winner, for ties.** Without it a tie has no honest representation. This item also needs a later firmware change: the firmware does not store a tied match today, and the 112-byte layout is fixed and has a winner field.
4. **Rate-limit the PIN lookup.** Without it the FDN's delay is the only brake on PIN guessing.
5. **Block a PDN or a player.** Without it nothing can shut out a PDN or a player caught cheating. Blocking by MAC address stops only an unmodified PDN, because the address is self-asserted. Blocking by player holds. A block does not revoke a key; it stops the server counting records under it.

### Superseded decisions

Each line is a decision of the earlier version of this proposal, what replaces it, and why.

1. **Every record tagged, crash records included** -> match records only. A tag cannot make a crash record true, the server already treats device logs as unverified, and relayed crash records then need no server change.
2. **A random key issued per registration and kept by the server, up to 255 per MAC address, named by a key ID** -> one key derived from a server secret, the PDN's MAC address and the player. There is nothing to store, nothing to wrap around and no key ID.
3. **A fresh key at every registration that replaces the stored one** -> the same PDN and player always get the same key. With a derived key there is nothing to rotate.
4. **Tag computed when the record is sent** -> computed when the match ends and stored with the record, so a PDN registered to another player before the record is delivered still delivers it under the key of the player who played it.
5. **Key kept in the PDN's non-volatile storage across boots** -> volatile memory only. Registration happens on every boot before any match that is stored, and crash records need no key. Pending the owner (Open Questions, 4).
6. **A PDN without a key holds its match records** -> it sends them with the all-zero tag and the server keeps them as unverified. Holding them would lose every match on a PDN that never receives a key, for example while the server does not derive keys. Pending the owner (Open Questions, 5).
7. **Tag input of kind, MAC address, key ID and record bytes** -> the record bytes alone. The MAC address and the player are in the key, there is no key ID, and only one kind is tagged.
8. **Server keeps one record per match and a verified copy replaces an unverified one** -> the server keeps both players' copies and flags unconfirmed and contradicted matches. One record per match cannot show that the two players disagree.
9. **Only HTTP `422` retires a relayed crash record** -> `400`, `413`, `415` and `422` do. Today's server never answers `422`, and it answers the other three for a request it can never accept.
10. **Server-side operator actions "drop a MAC address's keys" and "alert on a burst of issues"** -> gone with stored keys. "Block" becomes item 5 of the ranked server list: block a PDN or a player.
11. **The accepted risk "anyone who registers a MAC address with a valid PIN is issued a working key"** -> restated as "a stolen PIN used to register a second PDN" (Risks), because the key now depends on the player as well as the MAC address.

The record trailer and the "unverified crash record" and "quarantine" server rules go with decisions 1, 2 and 8.

## Risks / Trade-offs

Where the design does not hold on its own:

- **Everything rests on the server being reachable only by FDNs and operators.** The server authenticates nothing. Every player endpoint that returns a PIN (`GET /players`, `GET /players/{id}`, `GET /players/by-pin/{pin}`), the device lookup, and `POST /quickdraw-matches` answer any caller. So anyone who can reach the server can read every PIN, can post matches directly today, and once server item 2 exists can be given the key for any MAC address and any player from the lookup. The tags then protect nothing against that person. Every PDN image built so far carries the Wi-Fi password and the server address, so until PDN Wi-Fi is removed and the password changed, anyone holding a PDN can reach the server. → The Wi-Fi password change comes before the server starts returning keys (Migration Plan). Whether the premise holds after that is the owner's first open question.
- **Between this change and server verification, a radio can submit any match.** Before server item 2 the server verifies nothing, and with this change an FDN takes match records from any PDN with no cable. Player UUIDs cross the radio in the clear. So any radio in range of an FDN can submit a match record for any two players, and today's server scores the first upload and drops the real one with `204`. On the cabled path this change replaces, that needed a cable or the MAC address of a PDN that was plugged in at that moment. → Not decided here; the owner chooses (Open Questions, 2).
- **A fabricated match cannot be contradicted.** A player can tag a wholly invented match, with a new match ID and any opponent's player UUID, under their own key. It verifies and counts at once. The opponent holds no record with that ID, so no copy can ever contradict it. It shows only as unconfirmed, the same standing as an honest match whose loser withheld their copy or whose opponent's PDN never stored it. → Reviewing unconfirmed matches before prizes are awarded is the only check.
- **Two PDNs without keys play matches that never count once the server verifies.** Both copies carry the all-zero tag and the match is unverified. A PDN is without a key when the key frame was lost, when the server returned none, and for every match stored before its first key.
- **A PDN gives no sign that it has no key or a wrong one.** A lost key frame, a wrong key from a fake FDN on the jack, or a key that does not belong to the profile's player all leave the PDN looking registered. Its matches that session are unverified. Registering again needs a restart, and nothing repairs the matches already stored.
- **A derived key cannot be revoked singly.** Changing a stolen PIN does not change the key. Changing the server secret revokes every key at once, and every PDN is then without a valid key until it restarts and registers again.
- **Fake crash records can shut out real ones, from radio range.** Crash records are untagged and the server ignores a repeat of a MAC address and crash number. A radio can submit fake records under a victim PDN's MAC address for crash numbers 1 to N first, and the real ones are then ignored. Before this change that needed access to the network; now it needs radio range of an FDN.
- **A wrong answer from the server side can destroy relayed crash records.** A server regression or a proxy that answers `400`, `413` or `415` retires up to `32` relayed crash records per upload cycle for good, because the PDNs' marks have already advanced.

Stated plainly:

- A PDN's owner can read its key, so a tag never proves a record is true.
- Radio frames are unencrypted, so records and the registration PIN travel in the clear. Anyone near an FDN while a player registers learns that player's PIN.
- A 4-digit PIN can be guessed. The FDN's delay only slows it.

Accepted by the owner:

- **Two players colluding.** Both copies agree on a false result and the match shows as confirmed. Nothing in this design detects it.
- **A stolen PIN used to register a second PDN.** The second PDN gets a valid key for that player. With the opponent's PIN a cheat can supply "the opponent's copy" and confirm a false result alone; by also claiming the opponent's MAC address the cheat receives the opponent's actual key. Changing the PIN afterwards does not revoke the key. → Blocking the player (server item 5) is the only response once it is noticed.
- **A fake FDN swallowing records.** A device that beacons and acknowledges makes PDNs retire records. The matches then show as unconfirmed or missing on the server. The records stay in the PDN's ring buffers until overwritten, but nothing rewinds a mark.
- **A loser withholding their copy.** The match counts on the winner's copy and shows as unconfirmed. A player who keeps their PDN away from FDNs, or wipes it, produces the same result as an honest player whose PDN broke.

Other risks:

- **The server secret.** Whoever learns it can derive every key.
- **A persistent radio can block registration at an FDN.** Wrong PINs from one cabled PDN, or from a radio that copies a Connected PDN's MAC address, sent each time a delay ends, hold the FDN's single lookup and start another `120,000 ms`. Registration at that FDN is blocked for as long as they continue; one wait is bounded at `120,000 ms` once they stop. The same sender can block registration today by keeping the one lookup busy. A server with no active event also keeps the delay running.
- **Restarting an FDN clears the delay.** Someone who can cycle an FDN's power guesses at the undelayed rate.
- **Junk can keep an FDN's record store full.** Junk leaves the FDN at each upload and can be refilled at once, so one radio can hold an FDN full for as long as it transmits while PDN ring buffers overwrite. Each acknowledgement to a new MAC address also takes a slot in the FDN's radio peer table, and each beacon from a new address takes one in the PDN's. → Bounded on the devices by capacity and the driver's eviction rule.
- **Every PDN in range answers the same beacon.** Replies can collide; the transport retries and the next beacon is another chance.
- **The FDN handles a key at every registration.** A compromised FDN firmware could copy keys as they pass.

## Migration Plan

1. **Rework the open PRs.** #426, #427 and #428 lose the key ID and the crash trailer from their frames, the record check and the FDN record store entries before they merge, so the `17`-byte trailer never ships.
2. **First firmware roll-out.** Frame formats change, so every FDN and PDN takes the same build, as with the other wire changes in the comms rewrite. Crash relay and match relay work against today's server. From this point any radio in range of an FDN can submit a match that today's server scores, unless the owner chooses otherwise (Open Questions, 2).
3. **Stored match records.** A PDN's match slots holding `112` bytes and no tag are read as carrying the all-zero tag. They are delivered, and they stop counting when verification is turned on.
4. **The server team builds; nothing is enabled yet** (proposals to the server team, in rank order): keep both copies and flag; item 2 in three parts, the lookup takes the PDN's MAC address and returns the derived key, match uploads carry the sender's MAC address, the raw bytes and the tag, and uploads are verified; optional winner; lookup rate limit; block a PDN or a player. The field formats are agreed in this step.
5. **Wi-Fi password, before the server returns keys.** PDN Wi-Fi is removed (its own change), then the venue Wi-Fi password is changed, which means rebuilding every FDN. Every PDN flashed so far carries the old password; until it is changed anyone holding a PDN can join the network and would be able to ask the lookup for any key.
6. **Second firmware roll-out, to every FDN.** The lookup and upload seams are filled with the formats agreed in step 4 and every FDN is rebuilt again. This can be the same rebuild as step 5.
7. **Item 2, first part enabled: the lookup returns keys.** Verification stays off. The server counts the first copy as it does today, and copies with the all-zero tag count. A PDN registered before this step sends all-zero tags until it restarts and registers again, and matches already stored keep their all-zero tags.
8. **Item 2, last part enabled: verification is turned on between events, or at an announced time.** From then on a copy with the all-zero tag never counts. Turning it on mid-event would stop counting the matches of every PDN that has not restarted since step 7 and every match stored before its key.
9. **Rollback.** The server can stop verifying at any time and count the first upload as today. Firmware rollback means reflashing every device.

## Open Questions

For the owner, each answerable yes or no:

1. Two questions about who can reach the server (Risks, first entry):
   - **1a. Is the server reachable only by FDNs and operators once the Wi-Fi password is changed?** This is a question of fact. If not, the tags protect nothing against anyone who can reach it.
   - **1b. Are the player endpoints that return a PIN (`GET /players`, `GET /players/{id}`, `GET /players/by-pin/{pin}`), the device lookup and `POST /quickdraw-matches` acceptable as they are?** This is a decision. Yes accepts that every PIN, and later every key, stays readable by anything that gets onto that network.
2. **Should FDNs keep taking match records only from a Connected PDN until the server verifies tags?** Yes is choice (a): match records stay cabled until then and `record-relay` gains that condition for match records. No is choice (b): the exposure between the first firmware roll-out and verification is accepted (Risks, second entry).
3. **Do you accept the FDN's delay after an unknown PIN with its numbers** (`2,000 ms`, doubling to `120,000 ms`, cleared after `600,000 ms` without a `404`, FDN-wide)? Yes accepts a wait of up to `120,000 ms` per registration after wrong PINs, triggered also by a radio or by a server with no active event, and the guessing time that remains: about 6 days for a specific PIN and about 86 minutes for some valid PIN among 200 players, per FDN. Note: the review brief gave "about 3 hours" for the second figure; that was wrong.
4. **Do you accept the record key in volatile memory only, lost at restart?** Yes accepts the core-dump exception (Decisions, "Key on the PDN"). The alternative is the key in non-volatile storage, where a flash dump of a powered-off PDN finds it.
5. **Do you accept that a PDN with no key sends its matches with the all-zero tag, and that those matches never count once the server verifies?**
