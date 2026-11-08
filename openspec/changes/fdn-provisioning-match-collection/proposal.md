## Why

The PDN loses WiFi (#246). It used WiFi for two things: looking up the player for an entered PIN, and uploading finished matches. The FDN keeps WiFi, so both move to the FDN, reached over the jack cable a PDN is already plugged into (#264). Match records also change shape to carry server UUIDs, teams, the winner, and a UTC timestamp for `POST /quickdraw-matches` (#298).

## What Changes

- The PDN has no HTTP client. Its WiFi credentials, upload state, and boot-time HTTP pump are removed.
- Registration keeps the PIN entry screen. The PDN sends the entered PIN to its cabled FDN; the FDN does the lookup and sends the player profile, or a lookup failure, back to that PDN.
- The provisioning reply carries the FDN's UTC wall clock. The PDN sets its own wall clock from it, so match timestamps are real UTC.
- Standalone matches go into a 32-record ring in NVS on the PDN instead of a 255-record JSON list. After the result screen the PDN goes straight to sleep.
- A cabled FDN collects uncollected records one at a time, persists them in a 64-record spool, and acknowledges each one. The PDN marks a record collected only on that acknowledgement.
- The FDN uploads its whole spool in one `POST /quickdraw-matches` batch from the same idle upload check that uploads hacks.
- Match records hold draw times as 32-bit unsigned values, so PDN and FDN builds agree on the record layout.

## Capabilities

### New Capabilities
- `match-collection`: PDN-to-FDN transfer of stored matches and FDN batch upload.
- `player-provisioning`: PIN lookup by the cabled FDN on behalf of a PDN.

### Modified Capabilities
- `quickdraw-match`: post-match persistence no longer uploads from the PDN.
- `player-fetch-flow`: fetch user asks the cabled FDN instead of the server.
- `http-date-clock-sync`: the PDN's wall clock comes from its FDN, not an HTTP `Date` header.

## Impact

- `lib/core`: `Match` record layout, wire packets, `WirelessManager` without an HTTP client, `PlatformClock::setUnixMilliseconds`, a connected-peer query on the remote device coordinator.
- `lib/alleycat-server`: FDN match spool and provisioning manager.
- `src/pdn`: match ring and collection in `MatchManager`, `FetchUserState`, `main.cpp`.
- `src/fdn`: spool and provisioning construction, idle upload state.
- Out of scope: the NFC bracelet reader. When it lands it calls the same FDN provisioning entry point, and PIN entry can go.
