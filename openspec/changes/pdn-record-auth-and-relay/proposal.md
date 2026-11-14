## Why

PDNs are losing Wi-Fi: only FDNs and the server will be on the network, and once PDN Wi-Fi is removed no PDN will hold the Wi-Fi credentials. Crash records and match records therefore have to reach the server through an FDN, and the specs today only let that happen while the PDN is Connected on an FDN jack. A PDN that crashes and is never plugged in never reports.

Match results decide prizes, and a few technical attendees may read out a PDN's flash. Flash encryption and secure boot are off, so a PDN's owner can read any key it holds. A tag therefore shows which PDN and player sent a match record, not that the record is true. What catches a player lying about a result is comparing the two PDNs' copies of the same match. Tags are what make that comparison mean something over a radio on which a MAC address can be copied.

This revision replaces the earlier design of this change after a threat-model review. design.md, "Superseded decisions", lists each decision it replaces and why.

## What Changes

- Crash records and match records go PDN -> FDN over the peer-to-peer radio without a Connected jack, in one record frame for both kinds. A PDN sends to the FDN whose beacon it hears, or that it has just been plugged into, by reliable unicast, and advances its mark only when that FDN acknowledges having stored the record. The cable is optional for records, never required.
- The gaps in the current crash path are closed: a record is no longer retired when the radio reports a broadcast sent; delivery is retried on every FDN beacon, not once per boot; the FDN receives, stores, acknowledges and periodically uploads crash records; the PDN build uses this path instead of uploading over Wi-Fi.
- Match records carry a `16`-byte record tag: truncated HMAC-SHA-256 over the `112` match record bytes, under a `32`-byte record key. The tag is computed when the match ends and stored with the record. A PDN that holds no record key stores and sends the record with an all-zero tag.
- The record key is derived by the server from a server secret, the PDN's MAC address and the player. The FDN's lookup names the PDN; the FDN passes the key to the PDN over the serial jack at registration, never over the radio. The PDN holds it in volatile memory for the session only.
- Crash records carry no tag. The server treats every device log as unverified today, so relayed crash records need no server change.
- An FDN makes no PIN lookup for `600,000 ms` after the server has answered `5` lookups within `600,000 ms` with "no such PIN".
- FDNs relay records and tags unchanged and do not verify them. An FDN retires a relayed crash record the server can never accept (`400`, `413`, `415`, `422`) and continues with the next.
- The `Authorization: Bearer` wording is dropped from the three modified requirements that carried it, following the decision that the network has no device authentication.
- Registration, symbol match, learning to play and clock propagation stay cabled.
- Known limits, each accepted on the terms set out in design.md under Risks: two players can agree on a false result; a stolen PIN registers a second PDN that gets a valid key for that player; a device posing as an FDN can make PDNs retire records; a loser can withhold their copy; records and the registration PIN cross the radio in the clear.
- **BREAKING** (wire format): crash records and match records move to a new record frame, and the PDN's stored match records and the FDN's stored entries change size. Every PDN and FDN needs the same firmware. The three open relay PRs (#426, #427, #428) built the frame with a `17`-byte trailer that this revision removes.

## Capabilities

### New Capabilities
- `record-authentication`: the record key (derived by the server, held by the PDN for the session, never sent over the radio) and the record tag (match records only, computed at match end, stored with the record, all-zero when the PDN holds no key).
- `record-relay`: choosing an FDN by beacon or jack, the record frame, sending on the stored acknowledgement, retry triggers, and what an FDN does with a record from any PDN.

### Modified Capabilities
- `crash-reporting`: PDN crash records are sent untagged as record frames with no cable needed; the FDN stores up to `32`; a `400`, `413`, `415` or `422` answer retires a relayed record.
- `match-collection`: the record tag is stored with the match record; match records are sent as record frames with no cable needed; the upload carries the sender's MAC address, the record bytes and the tag.
- `fdn-apps`: the FDN collects records from any PDN, Connected or not; its beacon identifies it as an FDN. The requirement "Cabled background services on any Connected PDN" is renamed "Background services for Connected and nearby PDNs".
- `player-provisioning`: the lookup names the PDN; a record key is delivered over the serial jack and held for the session; the FDN limits PIN lookups after repeated "no such PIN" answers.

## Impact

- **Builds on** `pdn-pin-entry-fdn-registration` (archived), which has the PDN send its PIN to the FDN; a record key is accepted only after the PDN has sent it.
- **PDN firmware**: a record key in volatile memory, a tag computed at match end and stored in the match ring, a serial-jack frame for the key, and the relay sender from #427 and #428 with the trailer removed from its frames.
- **FDN firmware**: the record store and intake from #426 with the key ID removed from every entry and the tag kept for match records only; crash records on the periodic uplink with the retire rule; key relay over the jack; the PIN lookup limit; new match upload fields behind a marked seam.
- **Server (outside this repository)**: a ranked list of five proposals to the server team in design.md, "Ranked server list". Crash relay needs none of them. Match records upload in today's shape until the server team agrees the new fields; until item 2 is built PDNs hold no key and send all-zero tags.
- **Operations**: the venue Wi-Fi password must be changed once PDN Wi-Fi is removed, because every PDN image built so far carries it.
