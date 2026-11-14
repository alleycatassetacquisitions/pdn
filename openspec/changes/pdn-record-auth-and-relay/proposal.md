## Why

PDNs are losing Wi-Fi: only FDNs and the server will be on the network, and once PDN Wi-Fi is removed no PDN will hold the Wi-Fi credentials. Crash records and match records therefore have to reach the server through an FDN, and the specs today only let that happen while the PDN is Connected on an FDN jack. A PDN that crashes and is never plugged in never reports.

Match results decide prizes, and a few technical attendees may read out a PDN's flash. What catches a player lying about a result is comparing the two PDNs' copies of the same match. Tags are what make that comparison mean something over a radio on which a MAC address can be copied. What a tag does and does not show is in design.md, "What a tag is for".

## What Changes

- Crash records and match records go PDN -> FDN over the peer-to-peer radio without a Connected jack, in one record frame for both kinds. A PDN sends to the FDN whose beacon it hears, or that it has just been plugged into, by reliable unicast, and advances its mark only when that FDN acknowledges having stored the record. The cable is optional for records, never required.
- The gaps in the current crash path are closed: a record is no longer retired when the radio reports a broadcast sent; delivery is retried on every FDN beacon, not once per boot; the FDN receives, stores, acknowledges and periodically uploads crash records; the PDN build uses this path instead of uploading over Wi-Fi.
- Match records carry a `16`-byte record tag: truncated HMAC-SHA-256 over the `112` match record bytes, under a `32`-byte record key. The tag is computed when the match ends and stored with the record. A PDN that holds no record key stores and sends the record with an all-zero tag.
- The record key is derived by the server from a server secret, the PDN's MAC address and the player. The FDN's lookup names the PDN; the FDN passes the key to the PDN over the serial jack at registration, never over the radio. The PDN holds it in volatile memory only.
- Crash records carry no tag. The server treats every device log as unverified today, so relayed crash records need no server change.
- After a PIN lookup the server answers with `404`, an FDN makes no lookup for a delay that starts at `2,000 ms` and doubles with each further `404` up to `120,000 ms`.
- FDNs relay records and tags unchanged and do not verify them. An FDN retires a relayed crash record the server can never accept (`400`, `413`, `415`, `422`) and continues with the next.
- The `Authorization: Bearer` wording is dropped from the three modified requirements that carried it, following the decision that the network has no device authentication.
- Registration, symbol match, learning to play and clock propagation stay cabled.
- Where the design does not hold on its own (design.md, Risks): everything rests on the server being reachable only by FDNs and operators, because the server authenticates nothing and returns every player's PIN to any caller; until the server verifies tags, any radio in range of an FDN can submit a match that the server scores; a player can tag an invented match that can never be contradicted.
- Known limits accepted by the owner: two players can agree on a false result; a stolen PIN registers a second PDN that gets a valid key for that player; a device posing as an FDN can make PDNs retire records; a loser can withhold their copy.
- Records and the registration PIN cross the radio in the clear, as today.
- Six questions wait on the owner (design.md, Open Questions 1a, 1b and 2 to 5).
- **BREAKING** (wire format): crash records and match records move to a new record frame, and the PDN's stored match records and the FDN's stored entries change size. Every PDN and FDN needs the same firmware.

## Capabilities

### New Capabilities
- `record-authentication`: the record key (derived by the server, held by the PDN in volatile memory for the session at most, sent to no device) and the record tag (match records only, computed at match end, stored with the record, all-zero when the PDN holds no key).
- `record-relay`: choosing an FDN by beacon or jack, the record frame, sending on the stored acknowledgement, retry triggers, and what an FDN does with a record from any PDN.

### Modified Capabilities
- `crash-reporting`: PDN crash records are sent untagged as record frames with no cable needed; the FDN stores up to `32` and uploads them at boot and on its periodic uplink, its own records first; a `400`, `413`, `415` or `422` answer retires a relayed record.
- `match-collection`: the record tag is stored with the match record; match records are sent as record frames with no cable needed; the upload carries the sender's MAC address, the record bytes and the tag.
- `fdn-apps`: the FDN collects records from any PDN, Connected or not; its beacon identifies it as an FDN; the registration screen says when lookups are paused. The requirement "Cabled background services on any Connected PDN" is renamed "Background services for Connected and nearby PDNs".
- `player-provisioning`: the lookup names the PDN; a record key is delivered over the serial jack; the FDN delays lookups after each `404`.

## Impact

- **Builds on** `pdn-pin-entry-fdn-registration` (archived), which has the PDN send its PIN to the FDN; a record key is accepted only after the PDN has sent it.
- **PDN firmware**: a record key in volatile memory, a tag computed at match end and stored in the match ring, a serial-jack frame for the key, and the relay sender of #427 and #428 reworked to the record frame here.
- **FDN firmware**: the record store and intake of #426 reworked to the record frame here; crash records on the periodic uplink with the retire rule; key relay over the jack; the delay after an unknown PIN; the lookup's MAC address and the new match upload fields behind marked seams until the server team agrees their format.
- **Server (outside this repository)**: one precondition and a ranked list of five proposals to the server team in design.md, "Ranked server list". Crash relay needs none of them. Match records upload in today's shape until the server team agrees the new fields; until item 2 is built PDNs hold no key and send all-zero tags.
- **Operations**: the venue Wi-Fi password must be changed once PDN Wi-Fi is removed and before the server returns keys, because every PDN image built so far carries it. Filling the seams is a second firmware roll-out to every FDN.
