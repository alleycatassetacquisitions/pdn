## Wire contract

- **FDN beacon**: `PlayerInfoPkt` gains a trailing `senderDeviceType` byte. The FDN beacons every 12 s from its tick, in every app.
- **Packet types**: `kCrashRecord` 23, `kMatchRecord` 24, `kRecordAck` 25, after `kFirmwareUpdate` 22. `kCrashLog` 21 stays reserved; its dead handler goes.
- **Frame**: `{u8 seqId, u8 kind, u32 recordNumber, content}`. Crash, kind 0x01: 42-byte crash packet, 48 B. Match, kind 0x02: 112-byte record, 118 B. No tag.
- **Ack**: `{u8 seqId, u8 kind, u32 recordNumber, u16 crc16(content)}`, 8 B, sent only after a durable write and re-sent for a duplicate. The CRC names the frame; it is not authentication.

## PDN

- Ring in namespace `match_ring`, 32 slots. A new match is refused with `LOG_E` while 32 are unsent; nothing unacked is overwritten.
- A record retires only on an ack matching kind, number, CRC and the FDN MAC it was sent to. A delivery report never retires.
- One frame per kind in flight; none during a match or shootout; crash and match lanes independent.
- Older-firmware JSON matches (namespace `matches`) are kept untouched: no path reads them for relay, writes or erases them. They wait for a server route.
- Test-only identity: `esp32-s3_pdn_test` (`-DPDN_TEST_PLAYER`) and the simulator stamp a MAC-derived UUID: the MAC reversed plus tail `0000-7e57-7e577e577e57`, version nibble 0, never a server id. Release builds: ChooseTeam with a nil UUID; matches are not stored.

## FDN

- Store in namespace `fdn_records`: 32 matches, 16 crash records. Full: refuse without ack. Dedupe by sender, kind, number and bytes. Unknown kind: dropped without ack.
- Upload at boot and every 300 s while records wait, skipped while a jack is CONNECTED. Match batch first, then crash records oldest first.
- Retire on any decodable 2xx, including per-entry rejections. 400/413/415: probe the oldest alone, then retire it. 409/5xx/timeout: keep.

## Decisions

- **Beacon trigger over cable-only** (owner ruling). Crash records from PDNs never plugged into an FDN still arrive. Without tags, a device posing as an FDN can make PDNs retire records; the gate covers this. An FDN-side direct-peer check is possible later.
- **Boot-time `nvs_flash_erase`** on `NO_FREE_PAGES`/`NEW_VERSION_FOUND` stays: it is not a flash step, and neither fires with the current IDF and table. A framework bump must re-check it.
