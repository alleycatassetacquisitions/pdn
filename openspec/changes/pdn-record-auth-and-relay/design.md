## Context

Decisions this design rests on:

- PDNs have no Wi-Fi. The network holds only FDNs and the server, with no device authentication between them; the server validates what it receives.
- Crash records and match records travel PDN -> FDN over the peer-to-peer radio, then FDN -> server over Wi-Fi. The PDN picks an FDN from its beacon (every `12,000 ms`), sends each record by reliable unicast, and advances its mark only when the FDN acknowledges having stored the record.
- Registration, symbol match and learning to play stay on the cable. A Connected FDN may still receive records; records never require the cable.
- A match record is authenticated with a short tag, not a 64-byte signature. The firmware-update signing key is not reused.
- The tag is 16 bytes.
- An FDN holds up to `32` relayed crash records.
- Nothing is relayed during a match or a shootout.
- The `Authorization: Bearer` wording is removed from the requirements this change touches.
- Results decide prizes, and a few technical attendees may read out a PDN's flash. Flash encryption and secure boot are off.
- What catches a player lying about a result is comparing the two PDNs' copies of the same match. Tags make that comparison mean something over a radio on which a MAC address can be copied.
- A match counts on its first verified copy and is flagged if it is unconfirmed or contradicted.
- The record key is derived by the server from a server secret and the player alone. Nothing is stored per PDN on the server.
- Only match records are tagged. Crash records go untagged and unverified, as every device log is today.
- No flash encryption, no secure boot work, no eFuse key storage.
- The server team's capacity is unknown, so what this change asks of the server is a ranked list the team can cut from the bottom.

Facts from the owner:

- **The PIN is not a secret.** It is printed on the player's ID. It is an identifier, not a credential: it does not prove who holds the PDN.
- **PDNs are fungible.** A player often swaps to a different PDN during the event when a battery dies or a PDN crashes, and the PDN left behind may then be used by someone else.
- **Who can reach the server.** This is the owner's statement of fact and we have not verified it: once the venue Wi-Fi password is changed, the server is reachable only from the venue network, by FDNs and operators.

The owner's answers to the questions the previous revision left open, and the editor's choices still to confirm, are under Open Questions.

What the code does today on `comms-rewrite`, the branch under the three open relay PRs:

- **Match records** go only to the FDN Connected on a jack. The PDN re-sends a `116`-byte frame (4-byte sequence number + `112`-byte match record) every 100 ms as a plain, unsequenced send until the FDN answers. The FDN checks the sender is Connected, writes the record to a 64-slot non-volatile spool, then acknowledges; the PDN advances its mark on that acknowledgement.
- **The match record** is `112` bytes: match UUID (16), hunter draw time (4), bounty draw time (4), hunter player UUID (16), bounty player UUID (16), hunter team UUID (16), bounty team UUID (16), winner player UUID (16), occurred-at in UTC milliseconds (8). The two PDNs in a match store every field identically except occurred-at, which each stamps from its own clock.
- **Matches that are not stored**: a tie, a match in which neither side pressed, a shootout match, a match with a demo player, and a match on a PDN that never received the opponent's result. The last means honest play can leave one PDN with a record the other does not have.
- **Crash records** have a radio sender that nothing uses. It broadcasts each pending `42`-byte record once per boot and advances the sent mark when the radio reports the broadcast transmitted. No FDN code receives, queues or acknowledges a crash record. The PDN build is instead wired to upload over Wi-Fi itself.
- **Core dumps**: the build writes a core dump to flash on a crash.
- **FDN uploads.** Match records upload on a five-minute idle timer. The FDN's crash upload runs once, at boot, although `fdn-apps` already asks for it on that timer as well.
- **Registration** runs between a PDN and the FDN it is Connected to, but every message travels over the radio in the clear: a 6-byte request carrying the PIN, and a 584-byte profile back. The PDN keeps nothing from registration across boots and registers on every boot before it can play a match that is stored.
- **A registered PDN stays registered until it restarts.** The registration is cleared in one place, when the PIN entry screen mounts (`src/pdn/apps/player-fetch/pin-entry-state.cpp:17`), and the PDN reaches that screen only from the registration app it boots into (`src/pdn/main.cpp:139-141`). This is the handover rule: a PDN is handed to another player by restarting it (the `fungible-pdns` change).
- **A failed lookup** is reported to the PDN with one notification whatever the cause: the FDN discards the error, the HTTP status included (`lib/alleycat-server/include/alleycat-server/fdn-provisioning-manager.hpp:121-125`). The FDN shows `"UNABLE TO"` / `"LOCATE ASSET"` for `3000 ms` (`src/fdn/apps/idle/states/registration-state.cpp:73-75`, `src/fdn/apps/idle/idle-states.hpp:110`). The PDN shows `"Unable to"` / `"Locate Asset"` (`src/pdn/apps/player-fetch/fetch-user-state.cpp:122-129`) and returns to PIN entry.
- **The demo PIN** applies a profile with a nil player UUID without contacting an FDN or the server (`src/pdn/apps/player-fetch/choose-team-state.cpp:26`). The PDN plays, and a match with a nil player UUID on either side is not stored (`src/pdn/game/match-manager.cpp:251-255`).
- **Same-player matches** are refused on one side only. A PDN that receives a match invitation rejects it when the sender's player UUID equals its own non-nil player UUID (`src/pdn/game/match-manager.cpp:493-497`). The inviting PDN takes the opponent's player UUID from the acknowledgement unchecked (`src/pdn/game/match-manager.cpp:506-511`), and the match is stored without the two player UUIDs being compared (`src/pdn/game/match-manager.cpp:251-255`). With unmodified firmware on both PDNs the match never starts; against a modified opponent an unmodified PDN plays and stores a same-player record.
- **Stored match records are delivered whether or not the PDN is registered.** The relay is built at boot, before PIN entry (`src/pdn/main.cpp:147`), and sends on an FDN beacon or an FDN connection (`src/pdn/game/record-relay.cpp:58-90`). What stops a send is play being active, the radio not being in peer-to-peer mode, or a frame in flight (`src/pdn/game/record-relay.hpp:80-90`); registration is not consulted.
- **Stored match slots** are read only at exactly `112` bytes (`src/pdn/game/match-manager.cpp:352-353`). A slot of another length is passed over and logged, and the records after it are still sent (`lib/core/include/wireless/record-source.hpp:65-91`).
- **PIN requests** are accepted by the FDN from any radio source whose MAC address equals a Connected PDN's. A radio that copies a plugged-in PDN's MAC address can send PIN requests with no cable.
- **PIN lookups at the FDN.** The FDN makes one lookup at a time. It takes the PDN's MAC address from HELLO, which the PDN asserts itself, and it has two input jacks and does not know which jack a request came from.
- **FDN lookups** use whatever scheme the configured server address has, and fall back to plain HTTP when it names none.
- **Radio frames** are unencrypted and unauthenticated: ESP-NOW peers are added without encryption, no primary key is set, and nothing above the driver encrypts.
- **Serial jack**: carries only the 19-byte HELLO frame at 19200 baud. Its framing has an opcode byte, so a second frame type fits.
- **FDN beacon**: a broadcast every `12,000 ms`, but only while the FDN is on its idle or player-detected screen; it stops while a PDN is Connected. `fdn-apps` already asks for it whenever the radio is enabled. No PDN code listens for it.
- **Crypto in the PDN build**: HMAC-SHA-256 is available (mbedTLS message-digest layer, hardware SHA). ECDSA P-256 is compiled in; Ed25519 is not. The ESP32-S3 has no elliptic-curve accelerator.
- **Storage protection**: flash encryption, secure boot and NVS encryption are off in both builds.
- **Network credentials**: the Wi-Fi name, the Wi-Fi password and the server address are compiled into every PDN image built so far.

What the three open PRs add on top. #426 replaces the FDN's match spool with one record store for both kinds (`32` crash records, `64` match records), accepts record frames from any sender, acknowledges after storing with the record check, and makes the beacon identify its sender as an FDN and run in every FDN state. #427 adds the PDN's relay component with its three triggers, the one-in-flight rule and the play gate, moves crash records onto it, and removes the crash broadcast sender and the PDN's boot-time Wi-Fi crash upload. #428 moves match records onto the relay and removes the 100 ms re-send loop. Their frames and FDN record store entries still carry the `17`-byte trailer (key ID and tag, sent as zeros) that this revision removes. None of them uploads relayed crash records, changes an upload's fields, delivers a key or computes a tag.

#426 also adds a portable SHA-256 to the shared library, used for the record check.

#440, a PR of this change's firmware stack, removed the random player ID the PDN generated at boot, which base `player-provisioning` forbids ("never generating a random player ID at boot"). It needs no spec text.

What the server does today (alleycat-rs at `af107bf`; outside this repository):

- Nothing authenticates a caller. No endpoint has authentication, key issuance or key storage.
- Observed, with no ask: `GET /players`, `GET /players/{id}`, `GET /players/by-pin/{pin}`, `POST /players` and `PATCH /players/{id}` return the full player record, including `email` (nullable) and the PIN, with no authentication (`src/routes/players/mod.rs:13-31`, `get.rs:52-71,94-108`). The device lookup returns neither. The owner accepts these endpoints as they are.
- `POST /device-logs` takes `device_mac` as a field, so a relayed record under the PDN's MAC address is accepted today. A repeat of the same MAC address and crash number is ignored with `204`. A malformed body, invalid UTF-8 or a missing required field gets `400`; a body that is too large `413`; a wrong content type `415`. Nothing answers `422`.
- `POST /quickdraw-matches` accepts a match from any caller and rejects an unknown player or team per entry. A repeated match ID is stored once: the first upload wins and scores at once, and a second copy, even one naming a different winner, is dropped with `204`. The stored row records no uploader and no report count, and there is no view of disputed matches. A winner is required, so a tie cannot be stored. Occurred-at comes from the device and received-at from the server.
- `POST /quickdraw-matches` already refuses a match whose two players are the same player: the handler returns `SamePlayer` when `player_a_id == player_b_id` (`src/routes/quickdraw.rs:74-79`), and the table has the constraint `quickdraw_matches_distinct_players` (`migrations/20261011121000_create_quickdraw_matches.sql:34`).
- ADR 0001 ("Quickdraw results and scoring periods") records that both devices upload the same duel, sometimes late and with slightly different timestamps; that there is one result per match ID and the first valid upload supplies the result and the timestamp; that a small timestamp difference near a scoring boundary can change eligibility depending on which device uploads first; and that the server does not keep both reports. It says to "revisit this if reconciliation becomes necessary."
- `GET /device/players/by-pin/{pin}` takes only the path parameter, a 4-digit PIN, and nothing limits calls to it. So once the lookup returns keys (item 2 of the ranked list), anything on the venue network can list every player's key in 10,000 lookups. There is no way to block a player.

## Goals / Non-Goals

**Goals:**

- A PDN that is never plugged in still delivers its crash records and match records.
- The server can tell that a match record's tag was made with the record key of one of the two players named in the record, and that the record was not altered after it was tagged.
- The server can compare the two players' copies of a match and flag a match that is unconfirmed or contradicted.
- A record is never retired on the PDN because a frame was merely sent.
- An FDN needs no secrets to relay.
- Relayed crash records work against today's server.

**Non-Goals:**

- Proving a record is true.
- Proving who holds a PDN. The PIN is an identifier printed on the player's ID.
- Authenticating crash records.
- Authenticating FDNs to the server, the FDN's beacon or its acknowledgement (see Risks).
- Authenticating callers of the server. The owner accepts the unauthenticated endpoints as they are (Risks, "Accepted by the owner").
- Hiding record contents or the PIN. Both cross the radio in the clear as today, and the PIN is not a secret.
- Flash encryption, secure boot or eFuse key storage.
- A spec delta for removing PDN Wi-Fi. The base specs already say a PDN uses no Wi-Fi; the removal itself is a PR in this stack (Migration Plan).

## Decisions

### What a tag is for

Whoever holds a PDN can read its key, so a tag never proves a record is true: they can make the PDN tag any record. A tag that verifies says one thing: the tag was made with the record key the server returns when a PDN is registered to that player.

A false result is exposed by the other player's copy. The two PDNs in a match store the same match ID, players, teams, draw times and winner. If the server holds one copy tagged under each player's key and they agree, the result was reported under both players' keys. If they disagree, one was changed. If only one arrives, the match is unconfirmed. Exposed is not undone: the match has already counted on its first verified copy, and a flag changes no score until a person acts (Risks).

Without tags that comparison is worth little over this radio. Frames are unauthenticated and a MAC address can be copied, so one person with a radio could send both copies.

The comparison is only as strong as the path by which a key is handed out. A key reaches whoever enters the player's PIN on a PDN cabled to an FDN, and whoever can call the lookup directly. The PIN is printed on the player's ID, so a key does not show that the player themselves held the PDN (Risks).

### Record key: derived from the player alone, not stored

`record key = HMAC-SHA-256(server secret, player UUID (16 bytes))`, 32 bytes. The PDN's MAC address is not an input.

- The server derives the key on demand and stores nothing per PDN or per player (proposal to the server team; item 2 of the ranked list).
- A player gets the same key on every PDN they register on. PDNs are fungible (Context), so a player who swaps PDNs mid-event keeps one key, and match records tagged on the first PDN still verify.
- The server can derive the key for any record from the record itself: the two player UUIDs are in the record bytes.
- **Why the MAC address is not in it.** The address is self-asserted, in HELLO and over the radio, so binding the key to it protected nothing, and it gave the same player a different key on each PDN (Superseded decisions, 17).
- **A key lasts as long as the server secret.** It does not depend on the PIN, so changing a player's PIN does not change the key. Changing the server secret changes every key at once: every PDN's key is then wrong until that PDN restarts and registers again, and every stored match record not yet uploaded stops verifying. Copies the server verified before the change are not checked again.
- **Option for the server team, not part of this design:** a per-player version number in the derivation input. Raising one player's number would revoke that player's key alone. It costs one stored number per player.

### Key on the PDN: volatile memory only

- The PDN holds the 32-byte key in volatile memory and never writes it to the storage it keeps deliberately: its key-value storage and its match ring. The key is lost at restart.
- The PDN sends the key to no device and never logs or displays it.
- A key accepted later in the same registration attempt replaces the held one.
- The PDN drops the key whenever a registration attempt ends without a profile applied, so a key never outlasts a registration that failed.
- **Exception: core dumps.** The build writes a core dump to flash on a crash, and a core dump can contain the key if a copy is on a task stack or in a dumped region. The firmware wipes working copies after use and keeps the key off task stacks where practical (tasks.md 5.7). Overwritten after use: the PDN's key holder, the FDN's key holder, the jack hand-off's copy of the frame, the jack parser's buffers and the HTTP driver's delivered response buffer. Not overwritten: the IDF HTTP client's own receive buffer; the heap blocks the HTTP driver's response buffer outgrew while the response arrived; and a frame the jack parser has read part-way when the bytes stop, which stays until the next byte arrives on that jack. That narrows the exception and does not close it. The owner accepts it.
- A PDN whose player entered the demo PIN never contacts the server and holds no key. Its matches are not stored, as today.

### Registration: the key crosses the jack, and no key means no registration

In this document a PDN is *registered* when it holds a profile delivered by an FDN and the record key that came with it. A PDN whose player entered the demo PIN is not registered: it plays with the demo profile, which it applies itself with no FDN and no key (Context). Its connection context still marks a profile as applied, as today.

1. The PDN's registration request (radio, as today) is unchanged.
2. The FDN's lookup request is unchanged. It names no PDN.
3. The server returns the profile as today and, in the same response, the 32-byte record key for the looked-up player.
4. The FDN accepts a key only from a lookup made over HTTPS with the certificate verified, and only if it is exactly 32 bytes. In the firmware that HTTPS rule means: the FDN's configured server address is `https://` and the URL the transfer ended on is `https://`, with the server's certificate checked against the FDN's CA bundle. Redirects are followed, and the hops in between are not checked. The FDN holds the key in volatile memory only, never logs or stores it, and erases it as soon as the frame is written to the jack or the PDN disconnects.
5. The FDN writes the key to the serial jack in one new framed message. As soon as the jack reports the frame written, the FDN sends the profile over the radio as today, with no added wait.
6. **No key, no profile (editor's choice E1).** The FDN sends no profile in the cases it can detect: the lookup response carries no key it accepts; or the jack does not report the key frame written, which happens when the jack refuses the frame or the PDN is no longer Connected on that jack. A key of any length other than 32 bytes is no usable key; it does not make the response undecodable. In each case the FDN shows `"UNABLE TO"` / `"ISSUE KEY"` for `3000 ms`, the time of its existing failure screen, with one exception: when the PDN that asked has unplugged and no PDN remains Connected on either jack, the FDN leaves the registration screen at once, as it does today for any registration. If another PDN is still Connected on the other jack, the screen is shown for its `3000 ms`. A PDN that is still Connected gets the existing lookup-failure notification, with no new status value and no wire change. A PDN that has unplugged gets nothing, as today (`endLookup` in `lib/alleycat-server/include/alleycat-server/fdn-provisioning-manager.hpp`).
7. The PDN accepts a key only from a jack on which an FDN is Connected, and only in the provisioning app after it has sent that FDN its PIN. It discards a key whose frame fails its integrity check or that is not 32 bytes.
8. **Profile and key, in either order (editor's choice E2).** The key crosses the jack, about 20 ms at 19200 baud and read on the PDN's main loop, and the profile crosses the radio, so the profile can reach the PDN first. The PDN applies an FDN's profile only once it holds both a key accepted during this registration attempt and the delivered profile, whichever came first. A profile that arrives first is held, not applied, until the key arrives. If the attempt's existing `10,000 ms` limit runs out with either missing, the attempt fails as a timeout does today: `"Unable to"` / `"Locate Asset"`, then PIN entry, and whatever was held is dropped. A lookup-failure notification fails the attempt at once. So does the FDN the PDN sent its PIN to disconnecting from that jack: the PDN ends the attempt then, whatever it holds (key, profile or neither), and does not wait out the limit. The demo profile is not an FDN's profile and needs no key.

Nothing about the PDN or the key travels upward from the FDN to the server.

Where a failed hand-off shows. The FDN's `"ISSUE KEY"` screen tells staff the cause in the cases the FDN detects (step 6). One case only the PDN detects: the key frame is lost or damaged on the jack after the jack reported it written. There is no confirmation on the jack, so the FDN has sent the profile and shows its success screen, while the PDN holds the profile, waits out its `10,000 ms` limit and shows its failure screen. The player enters the PIN again. There is no new PDN screen.

The jack already carries a HELLO frame every 20 ms from its own task, so the key frame has to be written without interleaving with one.

An FDN's lookup can run over plain HTTP today (Context). Such a lookup yields no key the FDN accepts, so that FDN can register nobody with this firmware. What the server address and its certificate have to be is stated once, under Open Questions, E8.

Why the jack: registration messages cross the radio unencrypted, so a key sent that way could be read by any radio nearby. The jack is the one link an eavesdropper cannot hear without touching the device. A 32-byte payload at 19200 baud takes about 20 ms with its framing. The serial framing belongs to the component that tracks jack connections; it should carry this as one opaque application frame, checked for length and CRC and handed up with the jack it arrived on, so the meaning of the bytes and the rules for accepting them stay with registration.

### Tag: match records only, computed when the match ends

`tag = first 16 bytes of HMAC-SHA-256(record key, the 112 match record bytes)`.

- **Input.** The record bytes alone.
- **When.** The tag is computed when the match ends and stored with the record in the PDN's ring.
- **No key, no record (editor's choice E3).** A PDN stores a match record only together with a tag made under the key it holds. If a match ends and no key is held, the record is not stored. That cannot happen on a registered PDN, which holds a key by definition. It is what happens on a PDN playing with the demo profile, whose matches are not stored today either. No tag value means "no tag".
- **Storage.** A stored match record grows from `112` to `128` bytes on the PDN. A slot written by older firmware holds `112` bytes and no tag. A slot that is not `128` bytes cannot be sent and is not pending: it is never sent and the records after it are sent as if it were not there (editor's choice E4). Stored matches are uploaded with the current firmware before a PDN is flashed (Migration Plan).
- **Stored tags are never touched.** Registering the PDN, to the same player or to another, and restarting it leave the stored records and their tags as they are, and a PDN that is not registered still delivers them. The server finds the key from the players named in the record, not from who holds the PDN when it is delivered.
- **Crash records carry no tag.**
- **Implementation.** HMAC-SHA-256 is built on the portable SHA-256 in the shared library, so native tests and the PDN build run the same code. One fixed test vector is in `record-authentication` and is shared with the server team.
- **Why 16 bytes.** A forged tag can only be tested by submitting a record to the server, and each attempt succeeds with chance 2^-128. 16 bytes also meets RFC 2104 section 5 (at least 80 bits and at least half the hash length). The length is fixed once devices ship.
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
- **Triggers.** A beacon heard, an FDN becoming Connected, or a stored acknowledgement arriving. On the first two the PDN sends the lowest pending record of each kind; on the third, the next record of that kind. A record with no acknowledgement is simply sent again on the next beacon. A stored slot that cannot be sent (a match slot that is not `128` bytes) is not pending, so "lowest pending" passes over it here and in the acknowledgement rule below.
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

It does not limit records per sender: a MAC address is trivial to forge, so a per-sender limit would stop nobody and could be turned against a real PDN. Capacity is the limit.

The FDN's record store keeps the sender's MAC address, the kind, the number, the record bytes and, for a match record, the tag, unchanged.

An FDN takes match records over the radio from any PDN, Connected or not, as designed. There is no interim cable-only condition. The exposure that would exist against a server that does not verify tags is kept away only by the roll-out gate, a procedure (Risks).

### FDN uploads

- **When.** One upload cycle runs at boot and on the periodic uplink, and sends everything waiting: match records, the FDN's own crash records and relayed crash records.
- **Order.** The match batch goes first, then the FDN's own crash records, then the relayed ones. Matches go first because a long crash queue must not starve them inside the cycle's `15,000 ms` window. The steps are independent: a failed match batch does not stop the crash records, and a failure on one of the FDN's own crash records ends that step for the cycle and does not stop the relayed ones. An own record whose sent mark cannot be moved after the server took it counts as such a failure.
- **One user of Wi-Fi at a time.** A PIN lookup and an upload cycle never hold Wi-Fi together. A cycle does not start while a lookup holds Wi-Fi, and a lookup whose request the FDN has already received waits, inside its `8000 ms` limit, for a running cycle to end. While a cycle has the radio on Wi-Fi the FDN hears nothing on the peer-to-peer radio. A PDN that sends its PIN then sends it again each time the radio gives up on it, until its `10,000 ms` wait ends. A cycle that ends inside that wait is followed by a normal registration; if the cycle outlasts the wait, the attempt times out and the player enters the PIN again, as today.
- **Crash records** upload in today's device-log shape: the originating PDN's MAC address and the decoded fields, with no new field. Today's server accepts this.
- **Retiring a relayed crash record.** The FDN retires it on success, and on `400`, `413`, `415` or `422`, each of which says the request itself can never be accepted; it then continues with the next relayed record. On any other failure it stops the relayed records and keeps them for the next cycle. Without the retire rule one record the server will never take would block the record store for good. The rule does not apply to the FDN's own crash records, which advance only on success as today. Crash numbers restart if a PDN's storage is erased; today's server then ignores the reused number with `204`, which also retires the record.
- **Match records** upload the decoded fields as today plus the raw 112 record bytes and the 16-byte tag. The upload also carries the sending PDN's MAC address as a note for troubleshooting only. That address is the radio source address of the record frame, which any radio can set, so nothing protects it. The server does not use it to verify a record, to decide whose copy a record is, or to block anything. The field format is the server team's.
- **The seams.** There are two, each marked `SERVER-SEAM(pdn-record-auth-and-relay)`. The key field in the lookup response is provisional, not empty: under answer 5 the firmware cannot be built or tested without decoding a key, so the firmware's copy of the lookup response gains `optional bytes record_key = 10` now and follows whatever name and number the server team chooses. The three new match upload fields stay unfilled: firmware sends today's upload shape until the server team agrees the formats, and that seam is filled before the roll-out (tasks.md 10.3). Until it is filled these are not met: the scenario "Uploaded match carries the record bytes, the tag and the sender note alongside the decoded fields" (`match-collection`); and in `record-relay`, the upload sentence of the requirement "FDN relays without verifying" with the upload part of its scenario "A record with a wrong tag is still relayed", and the requirement "Sender's MAC address on a match upload is a note only".

### Server: verification and match standing (proposal to the server team)

Everything in this section is a proposal to the server team. Each rule names the item of the ranked list it belongs to.

- **Verification (item 2).** For a match upload the server takes the two player UUIDs from the raw bytes, derives each player's key, and checks the tag under each in constant time. If the tag matches under player P's key, the copy is verified as P's copy. That one check establishes that the bytes are unchanged since they were tagged and that the tag was made with P's key. No MAC address enters any rule.
- **Unverified copy (item 2).** A copy whose tag verifies under neither player's key is kept as unverified, whatever the tag's value. It never counts and never confirms.
- **Decoded fields (item 2).** The decoded fields in an upload must equal the fields in the raw bytes; an upload in which they differ is rejected per entry.
- **First copy per player kept (items 1 and 2).** The server keeps the first verified copy per match and player. Two copies verified under the same player's key are one player's report, whichever PDNs sent them: a later, different one never replaces the first, never confirms it, and flags the match.
- **Counts (item 2).** A match counts on its first verified copy and scores at once, as the first upload does today.
- **Standing (items 1 and 2),** computed from the first verified copy per player:
  - *Confirmed*: a verified copy under each player's key, and the two agree.
  - *Unconfirmed*: a verified copy under only one player's key.
  - *Contradicted*: a verified copy under each player's key, and the two disagree.
  - *Unverified*: only unverified copies. The match does not count.
- **Flagged,** for a person to look at: unconfirmed and contradicted matches, which come with item 1; unverified matches, and a match with two different copies verified under the same player's key, which come with item 2.
- **"Agree"** means every field of the 112 bytes is equal except occurred-at: match ID, both players, both teams, both draw times and winner. The firmware stores those identically on both PDNs, and occurred-at differs slightly by design (ADR 0001).
- **The sender's MAC address (item 2)** is kept with the copy as a troubleshooting note and nothing else. It is self-asserted over the radio. It takes no part in verification, in deciding whose copy a record is, in standing, or in blocking.
- **Same-player matches.** The server already refuses a match whose two players are the same player (Context), so nothing is asked. Firmware gains the matching refusal (`quickdraw-match`, "Same-player match refusal").
- **Scoring period (item 1).** The first verified copy's occurred-at decides the scoring period, as today. The server should also flag a match whose two copies fall in different scoring periods.
- **Crash records** need no change. They are stored as device logs under the originating PDN's MAC address and are unverified, as every device log is. Proposal to the server team: keep crash records out of anything that counts or alerts per PDN.

### Ranked server list (proposal to the server team)

The server team's capacity is unknown. The items are in rank order so the team can cut from the bottom, with one consequence to weigh: item 2 gates the firmware roll-out (Migration Plan), so without item 2 this firmware is not rolled out to event devices. Crash relay on its own needs none of them. Items 1 to 4 are about match records. Item 5 is a feature and is ranked last.

1. **Keep both copies of a match and flag contradicted and unconfirmed ones.** Without it, a lie by whoever uploads first stands silently, which is today's behaviour. Built without item 2, the server cannot tell whose copy an upload is. It can then flag two different copies of a match ID, but it cannot tell the other player's copy from the same copy sent twice, and it cannot flag a match as unconfirmed. Unconfirmed matches will be common: a dead PDN holds its copy until it is revived near an FDN, and one never revived never sends it. The review list should be sized for that.
2. **The lookup response returns the record key for the looked-up player; match uploads carry the raw bytes and the tag, and are verified.** The lookup request does not change. The upload also carries the sender's MAC address as a troubleshooting note that no rule reads. Without this item firmware has no key, no PDN can be registered, and the comparison in item 1 can be defeated by anyone with a radio.
3. **Optional winner, for ties.** Without it a tie has no honest representation. This item also needs a later firmware change: the firmware does not store a tied match today, and the 112-byte layout is fixed and has a winner field.
4. **Block a player.** A block stops one player's records counting. It does not revoke a key, and it does not stop a person: the PIN is printed on the player's ID, so a blocked person can register as any player whose ID they have read (Risks). Blocking is by player only. Optional, for the server team: the lookup may also return no key for a blocked player. The FDN then refuses the registration and shows `"UNABLE TO"` / `"ISSUE KEY"`, which makes the block visible at the venue.
5. **Return the player's totals with the lookup (feature, not a security item).** For the looked-up player, over the matches the server scores in the active event: matches played, wins and average draw time, in the lookup response or in a companion call. The PDN shows them on its statistics screens so that a player who swaps PDNs does not see them reset. Optional: a current win streak; the firmware has no field for it yet, so showing it needs a later firmware change. The owner may re-rank this item. It does not gate the roll-out, whose gate is item 2's tag verification: a lookup with no totals registers the PDN as usual. The firmware side and the details are in the `fungible-pdns` change.

### Superseded decisions

Each line is a decision of the earlier version of this proposal, what replaces it, and why.

1. **Every record tagged, crash records included** -> match records only. A tag cannot make a crash record true, the server already treats device logs as unverified, and relayed crash records then need no server change.
2. **A random key issued per registration and kept by the server, up to 255 per MAC address, named by a key ID** -> one derived key. There is nothing to store, nothing to wrap around and no key ID. The derivation inputs are in entry 17.
3. **A fresh key at every registration that replaces the stored one** -> the same player always gets the same key. With a derived key there is nothing to rotate.
4. **Tag computed when the record is sent** -> computed when the match ends and stored with the record, so a PDN registered to another player before the record is delivered still delivers it under the key of the player who played it.
5. **Key kept in the PDN's non-volatile storage across boots** -> volatile memory only. Registration happens on every boot before any match that is stored, and crash records need no key. Confirmed by the owner (answer 4).
6. **A PDN without a key holds its match records** -> first replaced by "it sends them with an all-zero tag", and that in turn by entry 15: a PDN without a key is not registered and stores no match record.
7. **Tag input of kind, MAC address, key ID and record bytes** -> the record bytes alone. The player is in the key, there is no key ID, and only one kind is tagged.
8. **Server keeps one record per match and a verified copy replaces an unverified one** -> the server keeps both players' copies and flags unconfirmed and contradicted matches. One record per match cannot show that the two players disagree.
9. **Only HTTP `422` retires a relayed crash record** -> `400`, `413`, `415` and `422` do. Today's server never answers `422`, and it answers the other three for a request it can never accept.
10. **Server-side operator actions "drop a MAC address's keys" and "alert on a burst of issues"** -> gone with stored keys. "Block" becomes item 4 of the ranked server list: block a player.
11. **The accepted risk "anyone who registers a MAC address with a valid PIN is issued a working key"** -> restated as "one person with two PDNs" (Risks, accepted by the owner). The PIN is printed on the player's ID, so anyone who has read it can register a PDN as that player.

Superseded by the owner's answers to the open questions of the previous revision:

12. **An unranked precondition that the player endpoints, the device lookup and the match upload "must not be reachable by players"** -> removed. The owner accepts the endpoints as they are (answer 1b) and states that only FDNs and operators are on the venue network once the Wi-Fi password is changed (answer 1a). What follows from that is under Risks.
13. **A choice between (a) FDNs taking match records only from a Connected PDN until the server verifies and (b) accepting the exposure in between** -> neither. The firmware is not rolled out to event devices until the server verifies tags (answer 2).
14. **An FDN delay after an unknown PIN, its paused-lookups display, and the server item "limit the PIN lookup"** -> removed (answer 3). The PIN is not a secret, so slowing attempts at it protects nothing. The observation that the lookup also fails when no event is active goes with it: nothing in the firmware reads the status of a failed lookup.
15. **A PDN registered without a key, storing and sending match records with an all-zero tag that the server keeps as unverified** -> registration fails when no key arrives (answer 5). No match record without a tag exists, the all-zero tag has no meaning on the device or the server, and a slot written by older firmware is never sent.
16. **Enabling server item 2 in two steps, with a transition in which PDNs registered earlier send untagged records that still count** -> one firmware roll-out after the server has built and enabled item 2. No device in the field runs this firmware against a server that does not verify.
17. **A key derived from the server secret, the PDN's MAC address and the player; a lookup that names the Connected PDN's MAC address; verification under the sender's MAC address; blocking a PDN by MAC address** -> a key derived from the server secret and the player alone, a lookup request that does not change, verification from the record's two players, and blocking by player only. PDNs are fungible (Context), so a key bound to one PDN breaks on an ordinary swap. The MAC address is self-asserted over the radio, so binding the key to it protected nothing. The upload still carries the sender's MAC address, as a troubleshooting note that no rule reads.

The record trailer and the "unverified crash record" and "quarantine" server rules go with decisions 1, 2 and 8.

## Risks / Trade-offs

Where the design does not hold on its own:

- **A fabricated match cannot be contradicted.** A player can tag a wholly invented match, with a new match ID and any opponent's player UUID, under their own key. It verifies and counts at once. The opponent holds no record with that ID, so no copy can ever contradict it. It shows only as unconfirmed, the same standing as an honest match whose loser withheld their copy or whose opponent's PDN never stored it. → Reviewing unconfirmed matches before prizes are awarded is the only check.
- **Only the roll-out gate keeps unverified radio submissions away from the server.** With this firmware an FDN takes match records from any PDN with no cable, and player UUIDs cross the radio in the clear. Against a server that does not verify tags, any radio in range of an FDN could submit a match record for any two players, and today's server would score the first upload and drop the real one with `204`. That is not a planned state: this firmware is not flashed to event devices until the server verifies (Migration Plan). The gate is a procedure and nothing in the firmware enforces it. Flashing early, or the server ceasing to verify, opens the exposure.
- **This firmware cannot register a PDN against a server that returns no key, including today's server (editor's choice E6).** Registration on real devices cannot be exercised until a server or a test server returns keys.
- **An FDN whose server address and certificate do not meet E8 can register nobody with this firmware (editor's choice E8).** The FDN takes a key only from a lookup over HTTPS (Decisions, "Registration", step 4). A plain `http://` address, an address with no scheme, a self-signed certificate or one from a private certificate authority each leave every registration at that FDN failing (Open Questions, E8).
- **A flag does not change the score.** A match counts on its first verified copy and scores at once; scoring is not held. A contradicted match keeps the score of whichever copy arrived first until a person acts.
- **Blocking a player does not stop a person.** The PIN is printed on the player's ID, so a person whose player is blocked can register a PDN as any player whose ID they have read.
- **Matches stored by older firmware are lost on a PDN that cannot be powered before it is flashed (editor's choice E4).** That PDN cannot upload its stored matches first, and this firmware never sends them (Migration Plan).
- **A PDN can hold a wrong key and look registered.** A fake FDN on the jack can hand a PDN any 32 bytes and then a profile. The PDN shows nothing about its key. Its matches that session are unverified on the server. Registering again needs a restart, and nothing repairs the matches already stored.
- **A key cannot be revoked singly.** Changing a player's PIN does not change the key, and changing the server secret changes every key at once, with the effects listed under Decisions, "Record key". The per-player version number there is the server team's option for revoking one player's key.
- **Fake crash records can shut out real ones, from radio range.** Crash records are untagged and the server ignores a repeat of a MAC address and crash number. A radio can submit fake records under a victim PDN's MAC address for crash numbers 1 to N first, and the real ones are then ignored. Before this change that needed access to the network; now it needs radio range of an FDN.
- **A wrong answer from the server side can destroy relayed crash records.** A server regression or a proxy that answers `400`, `413` or `415` retires up to `32` relayed crash records per upload cycle for good, because the PDNs' marks have already advanced.

Stated plainly:

- Whoever holds a PDN can read its key, so a tag never proves a record is true.
- The PIN is not a secret. It is printed on the player's ID and crosses the radio in the clear at every registration. Registration proves nothing about who holds the PDN.
- Registering a second PDN as the same player is ordinary: players swap PDNs when a battery dies or a PDN crashes. It is not a risk in itself and nothing flags it.
- Radio frames are unencrypted, so records travel in the clear.
- A match in progress when a PDN dies is not stored on that PDN. If the opponent's PDN stored its copy, the match shows as unconfirmed.
- Unconfirmed matches will be common. A dead PDN holds its stored copies until it is revived near an FDN, and a PDN never revived never sends them. Server item 1's review list has to be sized for that.
- A device cabled to an FDN is handed the record key of any PIN it sends, and nothing limits how many PINs it sends. So one modified PDN at one FDN can collect every player's key, in `10,000` lookups at most, and with a key it can tag match records as that player. Anyone who can cable a device to an FDN can do this, and that is anyone holding a PDN, not only FDNs and operators. It is wider than the venue network's exposure ("The server's unauthenticated endpoints", below): changing the Wi-Fi password does not stop it, and nothing in the firmware does. It follows from answer 3, answer 5 and the player-only key, and waits on the owner's acceptance (Open Questions, E9).

Accepted by the owner:

- **Two players colluding.** Both copies agree on a false result and the match shows as confirmed. Nothing in this design detects it.
- **One person with two PDNs.** One person can register one PDN as themselves and a second as any player whose ID they have read, play the two against each other, and produce two verified, agreeing copies. The match shows as confirmed. The match names two different players, so a check that refuses a match whose two players are the same player, on the PDN or on the server, does not catch it. → Once it is noticed, the response is to block the cheat's own player (server item 4). Blocking the impersonated player would punish the victim, so that player's key stays usable, and the cheat can do the same with another player's ID.
- **The server's unauthenticated endpoints.** The server authenticates nothing, and the owner accepts the endpoints as they are. The consequence: anything on the venue network can read every player's record, `email` included, and with it every PIN, which is all it takes to register a PDN as any player; it can post matches directly; and once the lookup returns keys it can ask for any player's key. The owner states that only FDNs and operators are on that network once the Wi-Fi password is changed; we have not verified it.
- **The key in a core dump.** A core dump written to flash on a crash can contain the record key (Decisions, "Key on the PDN").
- **A fake FDN swallowing records.** A device that beacons and acknowledges makes PDNs retire records. The matches then show as unconfirmed or missing on the server. The records stay in the PDN's ring buffers until overwritten, but nothing rewinds a mark.
- **A loser withholding their copy.** The match counts on the winner's copy and shows as unconfirmed. A player who keeps their PDN away from FDNs, or wipes it, produces the same result as an honest player whose PDN broke.
- **A running PDN handed to someone else stays registered as the previous player.** Nothing returns a PDN from play back to registration during a session (Context). A PDN passed on without a restart keeps the previous player's key and tags the new holder's matches as the previous player's, and they verify. The owner's rule is "restart to hand over", and what happens without a restart is accepted under it. The rule, and the player's name on the idle screen that goes with it, are in the `fungible-pdns` change.

Other risks:

- **The server secret.** Whoever learns it can derive every key.
- **Junk can keep an FDN's record store full.** Junk leaves the FDN at each upload and can be refilled at once, so one radio can hold an FDN full for as long as it transmits while PDN ring buffers overwrite. Each acknowledgement to a new MAC address also takes a slot in the FDN's radio peer table, and each beacon from a new address takes one in the PDN's. → Bounded on the devices by capacity and the driver's eviction rule.
- **Every PDN in range answers the same beacon.** Replies can collide; the transport retries and the next beacon is another chance.
- **The FDN handles a key at every registration.** A compromised FDN firmware could copy keys as they pass, and a player's key is the same on every PDN.
- **Redirect hops are not checked.** The FDN's HTTPS rule looks at its configured server address and at the URL the transfer ended on. A redirect in between is followed without being checked.

## Migration Plan

The steps are in the order in which things reach event devices. Firmware development does not wait for the server; flashing event devices does.

1. **Rework the open PRs.** #426, #427 and #428 lose the key ID and the crash trailer from their frames, the record check and the FDN record store entries before they merge, so the `17`-byte trailer never ships.
2. **The server team builds item 2** (and, in rank order, as much of the rest of the ranked list as it takes on): the lookup response returns the record key for the looked-up player, and match uploads carry the raw bytes and the tag and are verified. The field formats are agreed in this step: the key field in the lookup response, and the record bytes, tag and sender note on the upload.
3. **The firmware follows the agreed formats.** The provisional key field in the firmware's copy of the lookup response takes the name and number the server team chose, and the match upload seam is filled (tasks.md 10.3). Until a server or a test server returns keys, registration on real devices cannot be exercised, because this firmware does not register a PDN that gets no key.
4. **Stored matches are uploaded before the server verifies and before a PDN is flashed (editor's choice E7).** Each PDN is brought to an FDN while both still run their current firmware, so its stored matches upload in today's shape to a server that still counts them. After flashing, a slot written by the older firmware (`112` bytes, no tag) is never sent. A PDN that cannot be powered before it is flashed loses the matches it had not delivered.
5. **The server enables item 2.** The lookup returns keys and uploads are verified, both at once. Devices on the older firmware upload matches with no tag, which a verifying server does not count, so no event runs between this step and the next (editor's choice E7).
6. **One firmware roll-out, after step 5.** Frame formats change, so every FDN and PDN takes the same build, as with the other wire changes in the comms rewrite. The stack is not flashed to event devices before the server verifies tags, and every firmware PR description of this change says so (tasks.md 10.7).
7. **PDN Wi-Fi is removed and the venue Wi-Fi password is changed, at the roll-out.** This is an operations step and the owner's plan; it gates nothing. A PR in this stack removes Wi-Fi from the PDN build, so at the roll-out PDN images carry no network credentials and FDN images are built with the new password. Until the password is changed, anyone holding a PDN flashed so far can join the network and, once the lookup returns keys, ask for any player's key.
8. **Rollback.** Firmware rollback means reflashing every device. If the server stops verifying while this firmware is on event devices, the exposure the gate prevents is open (Risks).

## Possible future directions

Not a requirement, and no firmware in this change: the owner would like to link the PIN to the "neoband", an NFC band players are given. Whether an FDN has an NFC reader, and what the band stores, are unknown; this change assumes neither.

## Open Questions

The owner has answered every question the previous revision asked. What remains is the owner's confirmation of the editor's choices in the second list (tasks.md 1.9).

**Answered by the owner**

- **1a. Who can reach the server.** Once the venue Wi-Fi password is changed, the server is reachable only from the venue network, by FDNs and operators. This is the owner's statement of fact; we have not verified it.
- **1b. The unauthenticated endpoints.** Accepted as they are. Anything on the venue network can read every player's record, `email` included, and with it every PIN, which is all it takes to register a PDN as any player. Once the lookup returns keys it can also ask for any player's key.
- **2. Match records over the radio before the server verifies.** Neither choice (a) nor (b). This firmware is not rolled out to event devices until the server verifies tags, and FDNs accept match records over the radio as designed.
- **3. FDN delay after a wrong PIN.** No FDN delay. No server item to limit the PIN lookup either. The point that the lookup answers `404` when no event is active is dropped, because nothing in the firmware reads the status of a failed lookup.
- **4. Record key in volatile memory only.** Yes. The key is lost at restart and the core-dump exception is accepted.
- **5. A PDN with no key.** None of the options offered. Registration fails when no key arrives, so a PDN with no key is not registered and no match record without a key exists.
- **Key derivation.** The server secret and the player alone. A player gets the same key on any PDN they register on, and the lookup request does not change. A per-player version number for revoking one player's key stays an optional note to the server team.
- **Verification.** The server derives the keys of the two players named in the record and checks the tag under each. No MAC address enters it.
- **The sender's MAC address on a match upload.** An unprotected note for troubleshooting only. The server does not use it to verify, to decide whose copy a record is, or to block.
- **Blocking.** By player only.
- **A player on several PDNs.** Not a problem and not flagged. A match is a problem if the same player is both competitors; the server already refuses those and firmware gains the same refusal.
- **One person with two PDNs registered as two different players.** Accepted (Risks).
- **Handing a PDN to another player.** By restarting it. A PDN passed on without a restart stays the previous player's; accepted (Risks; the `fungible-pdns` change).
- **The player's totals on the PDN.** A server item, ranked last as a feature and not a security item (ranked server list, item 5). It does not hold up the roll-out gate.

**Editor's choices, to confirm** (E1 to E6 follow from answer 5; E7 from answer 2 with E4; E8 from E1 with the HTTPS rule; E9 from answers 3 and 5 with the player-only key)

- **E1.** The FDN sends the profile only after the jack reports the key frame written. With no usable key (none, or one that is not `32` bytes), or when the jack does not report the frame written, it sends no profile and shows `"UNABLE TO"` / `"ISSUE KEY"` for `3000 ms`; a PDN still Connected gets the existing lookup-failure notification. When the PDN that asked has unplugged and no PDN remains Connected on either jack, the FDN leaves the registration screen at once instead, as today.
- **E2.** The PDN applies an FDN's profile only once it holds both a key accepted during that registration attempt and the delivered profile, in either order. If the attempt's `10,000 ms` limit runs out with either missing, it fails as a timeout does today and both are dropped. If the FDN it sent its PIN to disconnects from that jack, the attempt ends at once, whatever the PDN holds. The demo profile needs no key.
- **E3.** The all-zero tag is removed from the device and from the server rules. A match that ends with no key held is not stored, which is the demo profile's case.
- **E4.** A stored match slot from older firmware (`112` bytes, no tag) is not pending: it is never sent and later records are sent. Stored matches are uploaded before a PDN is flashed.
- **E5.** The native simulator has no FDN and runs no lookup while it runs, so it needs no key. Its tests and the core tests do: tests that expect a stored match register the player through one shared helper that also sets a fixed test key; the helpers that deliver a profile to a PDN (`makeProvisionPacket`, `test/test_core/player-fetch-tests.hpp:68`; `cliDeliverCabledProvision`, `test/test_cli/native-driver-tests.hpp:821`) also deliver a key frame on the fake jack; and the lookup-response test encoders carry the key in the provisional field.
- **E6.** This firmware cannot register a PDN against a server that returns no key, including today's server.
- **E7.** Stored matches are uploaded before the server enables verification, not only before a PDN is flashed, and no event runs between the server enabling verification and the roll-out (Migration Plan, steps 4 to 6).
- **E8.** The FDN's server address must start with `https://`, and the server's certificate must be issued by a public certificate authority in the bundle built into the firmware, for the host name in that address. A self-signed certificate, a certificate from a private certificate authority, an address whose name the certificate does not carry, an `http://` address and an address with no scheme each register nobody with this firmware. The FDN checks its configured address and the URL the transfer ended on; redirect hops in between are not checked.
- **E9, to confirm that the owner accepts it.** A device cabled to an FDN is handed the record key of any PIN it sends, with no limit on how many it sends, so one modified PDN at one FDN can collect every player's key in `10,000` lookups at most and then tag match records as any player. Anyone who can cable a device to an FDN can do this, which is anyone holding a PDN. This is not the venue network's exposure of answer 1b, which the owner accepted on the statement that only FDNs and operators are on that network. It is wider: changing the Wi-Fi password does not stop it, and nothing in the firmware does.
