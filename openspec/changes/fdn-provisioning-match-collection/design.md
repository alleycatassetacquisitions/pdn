## Context

The PDN and FDN already share a cable whose jacks the remote device coordinator tracks, with each peer's MAC and device type known once the link is Connected. ESP-NOW runs on both; only the FDN has WiFi. While the FDN is in WiFi mode it cannot send or receive ESP-NOW.

## Decisions

### The cable is the trust anchor
Every new frame is accepted only from the MAC of a peer on a Connected jack whose device type is the expected one (FDN for frames the PDN receives, PDN for frames the FDN receives). Frames of any other length than their packet's struct are dropped. The source MAC is not authenticated; spoofing it needs radio proximity while the real cable is in, which is an accepted risk for a LARP event. The PIN crosses ESP-NOW in cleartext both ways, and the PIN echo in the reply only filters stale replies; it authenticates nothing.

### Collection is application-acknowledged, outside the reliable channels
`ReliableChannel` clears a send on the radio's SEND_SUCCESS, which only means the FDN's radio heard the frame. Collection needs to know the FDN persisted the record, and needs the FDN to be able to refuse when its spool is full. So records and acknowledgements are plain frames on two packet types (`kMatchCollection`, `kMatchCollectionAck`), one payload size each. The PDN keeps one record in flight and resends it every 100 ms until acknowledged. The FDN deduplicates by match UUID, so a resent record is acknowledged without being stored twice.

### Provisioning is one request and one reply
The PDN sends the PIN once per fetch screen, as soon as an FDN is cabled. The FDN ignores requests while a lookup is in flight, and gives a lookup 8 s from the request, shorter than the PDN's 10 s wait from mounting, before leaving Wi-Fi, which fails it and sends LOOKUP_FAILED; an AP the FDN cannot join would otherwise keep it off ESP-NOW for good. The reply echoes the PIN, and the PDN drops a reply for any PIN but the one it is waiting on, so a late answer to an earlier attempt cannot register the wrong player. If the reply never comes, the PDN's 10 s fetch timeout shows the failure page and returns to PIN entry, so the player retries by re-entering the PIN.

### Wall clock rides on the provisioning reply
The PDN has no other UTC source. The FDN stamps the reply with its clock right after the lookup's HTTP `Date` header synced it. The PDN ignores a timestamp before 2025-01-01, the same floor the `Date` parser uses, so an FDN whose lookup answer carried no usable `Date` header does not set the PDN's clock to its boot epoch. Players are not persisted, so every boot re-registers and re-syncs.

### Upload retirement
The server keeps the first report of a match ID. Any decodable response, including one with per-match rejections, retires the batch. A transport error or an undecodable body keeps it. A batch stops short of a spool record that cannot be read, because that record would fail the whole batch; once the records ahead of it are retired it is the oldest, and it is dropped.

### Not carried over from #298
The PDN no longer sees upload responses, so the rejection handling #298 describes for the uploading device (returning to registration on an own-side `INVALID_UUID`, a visible submit-error counter) has no device to run on. Rejections are logged on the FDN.

## Risks

- The FDN only uploads from its idle screen, after 5 minutes there. A busy FDN can fill its spool; it then withholds acknowledgements and PDNs keep their records until it drains. A PDN whose ring wraps first loses its oldest uncollected matches.
- Player identity and opponent UUIDs in records are self-reported by PDNs. Integrity relies on the server cross-checking both participants' reports.
