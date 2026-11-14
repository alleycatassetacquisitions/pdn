## Why

PDNs are losing Wi-Fi: only FDNs and the server will be on the network, and once PDN Wi-Fi is removed no PDN will hold the Wi-Fi credentials. Crash records and match records therefore have to reach the server through an FDN, and the specs today only let that happen while the PDN is Connected on an FDN jack. A PDN that crashes and is never plugged in never reports.

Match results decide prizes, and a few technical attendees may read out a PDN's flash. What catches a player lying about a result is comparing the two PDNs' copies of the same match. Tags are what make that comparison mean something over a radio on which a MAC address can be copied. What a tag does and does not show is in design.md, "What a tag is for".

## What Changes

- Crash records and match records go PDN -> FDN over the peer-to-peer radio without a Connected jack, in one record frame for both kinds. A PDN sends to the FDN whose beacon it hears, or that it has just been plugged into, by reliable unicast, and advances its mark only when that FDN acknowledges having stored the record. The cable is optional for records, never required.
- The gaps in the current crash path are closed: a record is no longer retired when the radio reports a broadcast sent; delivery is retried on every FDN beacon, not once per boot; the FDN receives, stores, acknowledges and periodically uploads crash records; the PDN build uses this path instead of uploading over Wi-Fi.
- Match records carry a `16`-byte record tag: truncated HMAC-SHA-256 over the `112` match record bytes, under a `32`-byte record key. The tag is computed when the match ends and stored with the record. Stored tags are sent unchanged, including after a restart and after the PDN is registered to another player.
- The record key is derived by the server from a server secret and the player alone, so a player has the same key on any PDN. The lookup request does not change; its response gains the key. The FDN passes the key to the PDN over the serial jack at registration, never over the radio. The PDN holds it in volatile memory only.
- Registration fails when no key arrives. The FDN sends the profile only once the jack reports the key frame written; with no key it sends the existing lookup-failure notification and shows `"UNABLE TO"` / `"ISSUE KEY"`. A PDN applies a profile delivered by an FDN only once it holds both the profile and a key, in either order. So a registered PDN always holds a key and no match record without a tag exists. The demo PIN is unchanged: the PDN plays with the demo profile, holds no key, and stores no match, as today.
- A PDN refuses a match in which the opponent's player UUID equals its own non-nil player UUID, on the inviting side as well as the invited side, and stores no match record that names the same player twice. The server already refuses such a match.
- Crash records carry no tag. The server treats every device log as unverified today, so relayed crash records need no server change.
- FDNs relay records and tags unchanged and do not verify them. A match upload also carries the sending PDN's MAC address as a troubleshooting note that nothing may rely on. An FDN retires a relayed crash record the server can never accept (`400`, `413`, `415`, `422`) and continues with the next.
- The `Authorization: Bearer` wording is dropped from the three modified requirements that carried it, following the decision that the network has no device authentication.
- Registration, symbol match, learning to play and clock propagation stay cabled.
- This firmware is not rolled out to event devices until the server verifies tags. Every firmware PR description of this change says so.
- Where the design does not hold on its own (design.md, Risks): a player can tag an invented match that can never be contradicted; only the roll-out gate keeps unverified radio submissions away from the server; this firmware cannot register a PDN against a server that returns no key, including today's server; a running PDN handed to someone else stays registered as the previous player until it restarts.
- The PIN is not a secret: it is printed on the player's ID, so registration proves nothing about who holds the PDN. Registering a second PDN as the same player is ordinary, because players swap PDNs during the event.
- Known limits accepted by the owner: two players can agree on a false result; one person can register two PDNs as two different players and produce two verified, agreeing copies; the server's endpoints are unauthenticated, so anything on the venue network can read every player's record, `email` included, and with it every PIN, which is all it takes to register a PDN as any player, and can ask for any player's key; a core dump can contain the key; a device posing as an FDN can make PDNs retire records; a loser can withhold their copy.
- Records and the PIN cross the radio in the clear, as today.
- **BREAKING** (wire format): crash records and match records move to a new record frame, and the PDN's stored match records and the FDN's stored entries change size. Every PDN and FDN needs the same firmware. Stored matches are uploaded with the current firmware before a PDN is flashed; a match slot written by older firmware is never sent afterwards.

## Capabilities

### New Capabilities
- `record-authentication`: the record key (derived by the server from the player, held by the PDN in volatile memory for the session at most, sent to no device) and the record tag (match records only, computed at match end, stored with the record, never changed afterwards; no match record is stored without one).
- `record-relay`: choosing an FDN by beacon or jack, the record frame, sending on the stored acknowledgement, retry triggers, and what an FDN does with a record from any PDN.

### Modified Capabilities
- `crash-reporting`: PDN crash records are sent untagged as record frames with no cable needed; the FDN stores up to `32` and uploads them at boot and on its periodic uplink, its own records first; a `400`, `413`, `415` or `422` answer retires a relayed record.
- `match-collection`: the record tag is stored with the match record; a match that ends with no record key held is not stored; a slot with no tag is never sent; match records are sent as record frames with no cable needed; the upload carries the record bytes and the tag, and the sender's MAC address as a troubleshooting note.
- `fdn-apps`: the FDN collects records from any PDN, Connected or not; its beacon identifies it as an FDN; the registration screen says when no key could be issued. The requirement "Cabled background services on any Connected PDN" is renamed "Background services for Connected and nearby PDNs".
- `player-provisioning`: the lookup response carries the record key; the key is delivered over the serial jack before the profile; with no key the FDN delivers no profile and the PDN applies none.
- `quickdraw-match`: a PDN refuses a match in which the opponent's player UUID equals its own non-nil player UUID, on either side of the handshake, and stores no record naming the same player twice.

## Impact

- **Builds on** `pdn-pin-entry-fdn-registration` (archived), which has the PDN send its PIN to the FDN; a record key is accepted only after the PDN has sent it.
- **PDN firmware**: a record key in volatile memory, an FDN's profile applied only with a key held, a tag computed at match end and stored in the match ring, a serial-jack frame for the key, the same-player refusal on the inviting side and at storage, Wi-Fi removed from the PDN build, and the relay sender of #427 and #428 reworked to the record frame here.
- **FDN firmware**: the record store and intake of #426 reworked to the record frame here; crash records on the periodic uplink with the retire rule; key relay over the jack with no profile when there is no key; a provisional key field in the firmware's copy of the lookup response, which follows the server team's choice; the new match upload fields behind a marked seam until the server team agrees their format.
- **Server (outside this repository)**: a ranked list of four proposals to the server team in design.md, "Ranked server list (proposal to the server team)". Crash relay needs none of them. Item 2 (the lookup response returns the key; uploads are verified) gates the roll-out: until a server returns keys no PDN can be registered with this firmware.
- **Operations**: one firmware roll-out to every FDN and PDN, after the server verifies tags. Before the server verifies, stored matches are uploaded with the current firmware. At the roll-out PDN images carry no Wi-Fi credentials, FDN images are built with a new venue Wi-Fi password, and the password is changed; this gates nothing. Until it is changed, anyone holding a PDN flashed so far can join the network.
- **Base requirement left as it is**: `quickdraw-match` "Post-match statistics, persistence, and result display" says a standalone match record "SHALL be appended" with no exception. This change does not modify it; the cases in which no record is stored (no record key held, the same player twice) are stated in `match-collection`, `record-authentication` and the new `quickdraw-match` requirement.
- **Other open change**: `match-records-nil-player` modifies `quickdraw-match` "Match initiation and identity". This change adds a separate requirement and does not touch that one.
