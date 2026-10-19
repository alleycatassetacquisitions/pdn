## 1. Proto resync and generation

- [x] 1.1 Copy `proto/device_api.proto` from `alleycat-rs@448b885` into `lib/alleycat-server/proto/device_api.proto`, and diff field numbers for `WriteDeviceLogRequest` against the current copy to confirm only comments changed
- [x] 1.2 Update `lib/alleycat-server/proto/upstream.lock.json` to commit `448b885c...` (full SHA from `git -C D:\projects\alleycat-rs rev-parse HEAD`)
- [x] 1.3 Extend `device_api.options`: `FT_CALLBACK` for all strings and repeated fields in `DevicePlayerResponse`, `DevicePlayerAffiliation`, `WriteQuickdrawMatchesResponse`, and `QuickdrawMatchRejection`; `max_size:37` for `QuickdrawMatch` string fields; `FT_CALLBACK` for `WriteQuickdrawMatchesRequest.matches`. Leave the `WriteDeviceLogRequest` options unchanged
- [x] 1.4 Run `python scripts/generate_proto.py` and commit the regenerated `device_api.pb.h` and `device_api.pb.c`
- [x] 1.5 Run the existing crash-encoder native tests (from WSL) to confirm `WriteDeviceLogRequest` encoding is unchanged

## 2. Device player lookup

- [x] 2.1 Add `include/alleycat-server/device-player.hpp` with `DevicePlayerAffiliation` and `DevicePlayer` (`hasTeam()` accessor)
- [x] 2.2 Add `device-player-decoder.hpp` and `src/device-player-decoder.cpp`: `bool DevicePlayerDecoder::decode(const std::string& body, DevicePlayer& player)` using nanopb string/repeated callbacks; return false on decode failure
- [x] 2.3 Add `PlayerApi::getDevicePlayerByPin` to `player-api.hpp`: queue `GET /device/players/by-pin/{pin}`, decode on success (otherwise `INVALID_RESPONSE`), forward errors unchanged
- [x] 2.4 Add `test/test_core/device-player-decoder-tests.hpp` covering: team + affiliations, unassigned player, inactive player, 200-byte name, garbage body. Build fixtures by encoding with nanopb in the test
- [x] 2.5 Add a request-shape test for `getDevicePlayerByPin` (`"0007"` → `GET /device/players/by-pin/0007`, empty payload)
- [x] 2.6 Register the new tests in `test/test_core/tests.cpp`

## 3. Quickdraw match upload

- [x] 3.1 Add `include/alleycat-server/quickdraw-match-codec.hpp` with `QuickdrawMatchRecord`, the `QuickdrawMatchErrorCode` enum class (with `UNKNOWN`), `QuickdrawMatchRejection`, `QuickdrawUploadResult` (`isFullyAccepted() const`), and the `MAX_MATCHES_PER_BATCH = 100` constant
- [x] 3.2 Implement `QuickdrawMatchCodec::encodeRequest(size_t count, const source&, std::string& payload)` in `src/quickdraw-match-codec.cpp`: string-appending `pb_ostream_t`, matches callback pulling each index exactly once, `has_` set for draw times and `occurred_at`, team IDs only when non-empty
- [x] 3.3 Implement `QuickdrawMatchCodec::decodeResponse(const std::string& body, QuickdrawUploadResult& result)`: empty body means fully accepted; otherwise decode rejections via callbacks, map codes, and keep `rawCode`; return false on decode failure
- [x] 3.4 Add `MatchesApi::uploadQuickdrawMatches` to `matches-api.hpp`: validate batch size 1–100 and encoding (otherwise `INVALID_STATE`, nothing queued), queue `POST /quickdraw-matches` as `application/protobuf`, decode on success (otherwise `INVALID_RESPONSE`), forward errors unchanged
- [x] 3.5 Add `test/test_core/quickdraw-match-codec-tests.hpp` covering: round-trip of all fields, zero draw time still present, absent vs present team IDs, a source returning false fails the encode
- [x] 3.6 Add response decode tests: empty body, a single `INVALID_EVENT_REFERENCE` rejection, unknown code 42 → `UNKNOWN`/`rawCode` 42, garbage body fails
- [x] 3.7 Add upload API tests: counts 0 and 101 rejected with no request queued; 1 queues `POST /quickdraw-matches` with content type `application/protobuf`
- [x] 3.8 Register the new tests in `test/test_core/tests.cpp`

## 4. Verification

- [x] 4.1 Run the full `native` test environment from WSL and confirm everything passes
- [x] 4.2 Build `esp32-s3_pdn_release` and confirm the library compiles for the device target and existing callers are unaffected
- [x] 4.3 Confirm no legacy JSON API, `Player`, `Match`, or game-state files were modified
