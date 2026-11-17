## Why

The Alleycat server has been rewritten (`alleycat-rs`) with a protobuf device API: players are looked up by PIN within the active event, and quickdraw matches are uploaded in batches with UUID participants, match-time team snapshots, explicit winners, and per-match rejections. `lib/alleycat-server` only speaks the legacy JSON endpoints (`GET /api/players/{id}`, `PUT /api/matches`), which the new server does not serve. This is stage 1 of the migration: give the firmware a tested client for the new contract before any game, storage, or radio code changes.

## What Changes

- Resync `lib/alleycat-server/proto/device_api.proto` with upstream `alleycat-rs` and update `upstream.lock.json` to the new commit.
- Extend `device_api.options` and regenerate the nanopb sources so the new messages can be encoded and decoded on device without large fixed buffers.
- Add a device player lookup client: `GET /device/players/by-pin/{pin}`, decoding `DevicePlayerResponse` into a library-owned `DevicePlayer` type.
- Add a quickdraw match upload client: `POST /quickdraw-matches`, encoding a batch of library-owned match records into `WriteQuickdrawMatchesRequest` and decoding `WriteQuickdrawMatchesResponse` into a result that lists rejected entries.
- Leave the legacy JSON clients (`PlayerApi::getPlayer`, `MatchesApi::updateMatches`, `PlayerResponse`) in place. Their callers move over and the legacy clients are removed in a later stage.

Out of scope for this stage: changes to `Player`, `Match`, `MatchManager`, `QuickdrawPacket`, game states, wall-clock time (provided separately by `http-date-clock-sync`), batch chunking across stored matches, and HTTP status codes in `WirelessErrorInfo`.

## Capabilities

### New Capabilities
- `device-player-lookup`: Fetching a player's registration from the Alleycat server by PIN using the protobuf device API.
- `quickdraw-match-upload`: Uploading a batch of completed quickdraw matches to the Alleycat server and reporting which entries were rejected.

### Modified Capabilities

None.

## Impact

- `lib/alleycat-server/proto/`: `device_api.proto`, `device_api.options`, `upstream.lock.json`.
- `lib/alleycat-server/include/alleycat-server/` and `src/`: regenerated `device_api.pb.h` and `device_api.pb.c`, plus new decoder/encoder headers and sources and new API functions in `player-api.hpp` and `matches-api.hpp`.
- `test/test_core/`: new native tests for the decoder, encoder, and request construction.
- No firmware behavior changes. Existing callers keep using the legacy JSON endpoints until a later stage migrates them.
- The crash-log message `WriteDeviceLogRequest` keeps the same wire format. Upstream only changed its comments.
