## Why

PDNs are losing Wi-Fi: only FDNs and the server will be on the network, and once PDN Wi-Fi is removed no PDN will hold the Wi-Fi credentials. Crash records and match records therefore have to reach the server through an FDN, and the specs today only let that happen while the PDN is Connected on an FDN jack. A PDN that crashes and is never plugged in never reports.

Once any PDN in radio range can hand records to an FDN, the server has no way to tell which PDN a record came from: the FDN no longer has a cable to vouch for the sender, and FDN requests carry no identity for the PDN that the server can verify. Each PDN needs to authenticate its own records.

## What Changes

- Crash records and match records go PDN -> FDN over the peer-to-peer radio without a Connected jack, in one record frame for both kinds. A PDN sends to the FDN whose beacon it hears, or that it has just been plugged into, by reliable unicast, and advances its mark only when that FDN acknowledges having stored the record. The cable is optional for records, never required.
- The gaps in the current crash path are closed: a record is no longer retired when the radio reports a broadcast sent; delivery is retried on every FDN beacon, not once per boot; the FDN receives, stores, acknowledges and periodically uploads crash records; the PDN build uses this path instead of uploading over Wi-Fi.
- Each PDN gets a fresh per-PDN record key at every cable registration, delivered over the serial jack and never over the radio; it replaces the key the PDN held. Every record is followed by a `17`-byte trailer: a key ID and a `16`-byte truncated HMAC-SHA-256 tag over the record kind, the PDN's MAC address, the key ID and the record bytes. A PDN holds its match records until it has a key.
- The `Authorization: Bearer` wording is dropped from the three modified requirements that carried it, following the decision that the network has no device authentication.
- FDNs relay records and trailers unchanged and do not verify them. The server verifies, treats repeats as one record, rejects a match record without a valid tag, and accepts a crash record without one as unverified; the FDN retires a permanently rejected record.
- Known limits, each accepted on the terms set out in design.md under Risks: a device posing as an FDN can make PDNs retire records; anyone who can reach the server's lookup can be issued a working key for any PDN; unverified crash records can be created under any MAC address.
- Registration, symbol match, learning to play and clock propagation stay cabled.
- **BREAKING** (wire format): crash records and match records move to a new record frame, the FDN's stored entries grow, and the registration lookup and both uploads gain fields. Every PDN and FDN needs the same firmware, and the server needs the matching API change first.

## Capabilities

### New Capabilities
- `record-authentication`: the per-PDN record key, the record tag and the record trailer.
- `record-relay`: choosing an FDN by beacon or jack, sending on the stored acknowledgement, retry triggers, and what an FDN does with a record from any PDN.

### Modified Capabilities
- `crash-reporting`: PDN crash records are sent as record frames with no cable needed; the FDN stores up to `32`; a `422` retires a relayed record.
- `match-collection`: match records are sent as record frames with no cable needed; the upload carries the signed bytes and trailer.
- `fdn-apps`: the FDN collects records from any PDN, Connected or not; its beacon identifies it as an FDN. The requirement "Cabled background services on any Connected PDN" is renamed "Background services for Connected and nearby PDNs".
- `player-provisioning`: the lookup names the PDN; a record key is delivered over the serial jack and replaces the one held.

## Impact

- **Builds on** `pdn-pin-entry-fdn-registration` (archived), which has the PDN send its PIN to the FDN; a record key is accepted only after the PDN has sent it.
- **PDN firmware**: record key storage, tag computation, beacon listening, one relay sender for both record kinds, a serial-jack frame for the key, and the crash reporter moved off Wi-Fi onto the relay.
- **FDN firmware**: one record store for both kinds with sender and trailer, the cable check on record intake removed, crash records on the periodic uplink, the beacon kept running while a PDN is Connected, key relay over the jack, new upload fields, `422` handling.
- **Server (outside this repository)**: the two sections of design.md marked as proposals to the server team: key rules (a fresh key at every player lookup, earlier keys kept, dropping and blocking) and record rules (tag verification on `POST /device-logs` and `POST /quickdraw-matches`, repeats, rejections, unverified crash records). Nothing here can ship before that contract is agreed.
