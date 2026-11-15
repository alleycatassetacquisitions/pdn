## Why

PDNs do not use Wi-Fi, so crash and match records reach the server only through an FDN. Today the PDN still uploads matches itself, FDNs store nothing they hear, and release flashing clears NVS, so a firmware update erases every stored match. Tests also need a player identity without a server.

## What Changes

- PDN→FDN record relay over ESP-NOW (`record-relay`): one frame per record, triggered by any FDN beacon, retired only on the FDN's stored acknowledgement. PDN match ring with player UUIDs in its own namespace; full ring refuses new matches.
- FDN record store; upload at boot and every 300 s to today's `POST /quickdraw-matches` and `POST /device-logs`, request formats unchanged.
- Matches stored by older firmware stay in NVS untouched and are not relayed; they wait for a server route. Release flashing never clears NVS.
- Test-only registration in a test build and the simulator; release builds have none. `Authorization: Bearer` is dropped from the upload requirements: neither firmware nor server uses it.

## Non-goals

Record tags or keys, band or PIN-via-FDN registration, fungible PDN features, transport refactors, storage partitions. The FDN credential lookup and profile delivery text in `player-provisioning` and `fdn-apps` is not implemented and stays for the registration project.

## Roll-out gate

This change is test-only and is not flashed to event devices until the registration project lands. Event devices stay on tag `event-firmware-2026-11` (`main` @ `1399f6f3`). This replaces the retired "server verifies tags" gate. Lifting the gate requires, in the registration project: real registration, and the storage partitions (#463 FDN `fdn_store`, #472 PDN match ring) so new matches survive a full NVS.

## Capabilities

New: `record-relay`. Modified: `match-collection`, `crash-reporting`, `fdn-apps`, `player-provisioning`, `firmware-distribution`. Impact: `platformio.ini`, `scripts/multi_flash_target.py`; later PRs touch `src/pdn`, `src/fdn` and `lib/core`.
