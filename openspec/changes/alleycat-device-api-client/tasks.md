## 1. Proto resync and generation

- [x] 1.1 Copy `proto/device_api.proto` from `alleycat-rs@448b885` into `lib/alleycat-server/proto/device_api.proto`, and diff field numbers for `WriteDeviceLogRequest` against the current copy to confirm only comments changed
- [x] 1.2 Update `lib/alleycat-server/proto/upstream.lock.json` to commit `448b885c...` (full SHA from `git -C D:\projects\alleycat-rs rev-parse HEAD`)
- [x] 1.3 Extend `device_api.options`: static `max_size` for all strings in `DevicePlayerResponse`, `DevicePlayerAffiliation`, and `QuickdrawMatchRejection` (37 for UUIDs/names, 5 for the PIN); `FT_CALLBACK` for `DevicePlayerResponse.affiliations`; `max_count:2 max_size:24` for rejection `fields`; `FT_CALLBACK` for `WriteQuickdrawMatchesResponse.rejected` and `WriteQuickdrawMatchesRequest.matches`; `max_size:37` for `QuickdrawMatch` string fields. Leave the `WriteDeviceLogRequest` options unchanged
- [x] 1.4 Run `python scripts/generate_proto.py` and commit the regenerated `device_api.pb.h` and `device_api.pb.c`
- [x] 1.5 Run the existing crash-encoder native tests (from WSL) to confirm `WriteDeviceLogRequest` encoding is unchanged

## 2. Device player lookup

- [x] 2.1 Add `include/alleycat-server/device-player.hpp` with `DevicePlayer` with fixed header fields, `std::vector<DevicePlayerAffiliation>`, and `DevicePlayerLimits::MAX_AFFILIATIONS = 3`
- [x] 2.2 Add `device-player-decoder.hpp` and `src/device-player-decoder.cpp`: `bool DevicePlayerDecoder::decode(const std::string& body, DevicePlayer& player)` static header decode plus an affiliations callback into the vector, with `static_assert`s on string buffer sizes; return false on decode failure
- [x] 2.3 Add `PlayerApi::getDevicePlayerByPin` to `player-api.hpp`: queue `GET /device/players/by-pin/{pin}`, decode on success (otherwise `INVALID_RESPONSE`), forward errors unchanged
- [x] 2.4 Add `test/test_core/device-player-decoder-tests.hpp` covering: team + affiliations, unassigned player, inactive player, 36-byte name accepted, 37-byte name rejected, 3 affiliations accepted, 4th rejected, garbage body. Build fixtures by encoding with nanopb in the test
- [x] 2.5 Add a request-shape test for `getDevicePlayerByPin` (`"0007"` → `GET /device/players/by-pin/0007`, empty payload)
- [x] 2.6 Register the new tests in `test/test_core/tests.cpp`

## 3. Quickdraw match upload

- [x] 3.1 Add `include/alleycat-server/quickdraw-match-codec.hpp` with `QuickdrawMatchRecord`, the `QuickdrawMatchErrorCode` enum class (with `UNKNOWN`), `QuickdrawMatchRejection`, `QuickdrawUploadResult` (`isFullyAccepted() const`), and the `MAX_MATCHES_PER_BATCH = 100` constant
- [x] 3.2 Implement `QuickdrawMatchCodec::encodeRequest(size_t count, const source&, std::string& payload)` in `src/quickdraw-match-codec.cpp`: string-appending `pb_ostream_t`, matches callback pulling each index exactly once, `has_` set for draw times and `occurred_at`, team IDs only when non-empty
- [x] 3.3 Implement `QuickdrawMatchCodec::decodeResponse(const std::string& body, const RejectionSink& onRejection, QuickdrawUploadResult& result)`: empty body means fully accepted; otherwise validate the whole body, then stream each rejection to the sink, map codes, and keep `rawCode`; return false on decode failure without calling the sink
- [x] 3.4 Add `MatchesApi::uploadQuickdrawMatches` to `matches-api.hpp`: validate batch size 1–100 and encoding (otherwise `INVALID_STATE`, nothing queued), queue `POST /quickdraw-matches` as `application/protobuf`, deliver rejections to `onRejection` then call `onSuccess` (otherwise `INVALID_RESPONSE`), forward errors unchanged
- [x] 3.5 Add `test/test_core/quickdraw-match-codec-tests.hpp` covering: round-trip of all fields, zero draw time still present, absent vs present team IDs, a source returning false fails the encode
- [x] 3.6 Add response decode tests: empty body, a single `SAME_PLAYER` rejection with two fields, 100 rejections delivered in order, unknown code 42 → `UNKNOWN`/`rawCode` 42, a third field fails without delivering anything, garbage body fails
- [x] 3.7 Add upload API tests: counts 0 and 101 rejected with no request queued; 1 queues `POST /quickdraw-matches` with content type `application/protobuf`
- [x] 3.8 Register the new tests in `test/test_core/tests.cpp`

## 4. UUIDBytes

- [x] 4.1 Add `UUIDBytes` in `lib/core/include/utils/uuid-bytes.hpp` and `lib/core/src/utils/uuid-bytes.cpp`: 16-byte value with shape-only `parse` (either case, `false` leaves output unchanged), lowercase `format`, `isNil`, `==`/`!=`. No dependency on `IdGenerator`
- [x] 4.2 Switch UUID fields in `DevicePlayer`, `DevicePlayerAffiliation`, `QuickdrawMatchRecord`, and `QuickdrawMatchRejection` to `UUIDBytes`; `hasTeam()` and the encoder's team check use `isNil()`
- [x] 4.3 Decoders parse UUID fields and fail on malformed text; the rejection decoder parses during the validation pass so nothing is delivered. Encoder formats into the message fields. `static_assert`s tie `UUIDBytes::TEXT_BUFFER_SIZE` to the generated fields and `IdGenerator::UUID_BUFFER_SIZE`
- [x] 4.4 Add `test/test_core/uuid-bytes-tests.hpp`: known value, either case, lowercase round trip, malformed text rejected with output unchanged, nil, equality. Update decoder and codec tests to real UUIDs; add malformed player/team/affiliation UUID and malformed rejection `match_id` cases
- [x] 4.5 Confirm `IdGenerator`, `Match`, `QuickdrawPacket`, and game-state files are unchanged

## 5. Verification

- [x] 5.1 Run the full `native` test environment from WSL and confirm everything passes
- [x] 5.2 Build `esp32-s3_pdn_release` and confirm the library compiles for the device target and existing callers are unaffected
- [x] 5.3 Confirm no legacy JSON API, `Player`, `Match`, or game-state files were modified
