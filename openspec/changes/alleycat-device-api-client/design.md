## Context

`lib/alleycat-server` is a header-mostly client library. Each endpoint is a namespace function that builds an `HttpRequest` and queues it on `WirelessManager` (`CrashApi`, `PlayerApi`, `MatchesApi`, `HealthApi`). Protobuf is already used for `POST /device-logs`: nanopb sources are generated from `proto/device_api.proto` by `scripts/generate_proto.py`, and `CrashEncoder` is a pure function covered by native tests in `test/test_core/crash-encoder-tests.hpp`.

The vendored proto is pinned to `alleycat-rs@09bffc6` and only contains `WriteDeviceLogRequest`. Upstream (`448b885`) adds `DevicePlayerResponse`, `DevicePlayerAffiliation`, `WriteQuickdrawMatchesRequest`, `QuickdrawMatch`, `WriteQuickdrawMatchesResponse`, `QuickdrawMatchRejection`, and `QuickdrawMatchErrorCode`.

The HTTP layer has two constraints this design has to work within:
- Success callbacks receive only the response body. The status code (200 vs 204) is not available.
- Error callbacks receive `WirelessErrorInfo { code, message }`. A 404 arrives as `INVALID_RESPONSE` with the message `"HTTP Error: 404"` and can't be told apart from other client errors.

## Goals / Non-Goals

**Goals:**
- Native-testable encode/decode for the new player lookup and match upload messages.
- Request builders for `GET /device/players/by-pin/{pin}` and `POST /quickdraw-matches` that follow the existing `CrashApi` pattern.
- Library-owned input/output types, so later stages can reshape `Player` and `Match` without touching this library's contract.
- No heap allocation while decoding, and bounded stack use on ESP32-S3.

**Non-Goals:**
- Migrating `FetchUserDataState` or `UploadMatchesState`, and removing the legacy JSON clients.
- Changes to `Player`, `Match`, `MatchManager`, `QuickdrawPacket`, or persisted storage.
- Producing `occurred_at_unix_ms`. The caller supplies the value, from `PlatformClock::unixMilliseconds()` added by the `http-date-clock-sync` branch.
- Splitting stored matches into batches of 100. The caller does this.
- Passing HTTP status codes through `WirelessErrorInfo`. This would change `lib/core` and both HTTP drivers.
- Semantic UUID validation (version, variant, nil in a required field). The decoder checks shape only; the server reports invalid entries one by one.
- Shootout match IDs, which are not UUID-shaped. Nothing outside this library adopts `UUIDBytes` in this stage.

## Decisions

### Library-owned types instead of `Player` / `Match`
The new API takes and returns types defined in `lib/alleycat-server`:
- `DevicePlayer`: `UUIDBytes` `id`, `teamId` (nil when unassigned; `hasTeam()`), fixed `char[]` `name`, `teamName`, and `std::vector<DevicePlayerAffiliation> affiliations` (at most `DevicePlayerLimits::MAX_AFFILIATIONS`, currently 3). Each affiliation has `UUIDBytes` `categoryId`/`affiliationId` and fixed `char[]` names.
- `QuickdrawMatchRecord`: `UUIDBytes` for match, player A/B, winner, and team A/B (nil means no team), plus `uint64_t` draw times and `int64_t occurredAtUnixMs`.
- `QuickdrawMatchRejection`: `matchIndex`, `UUIDBytes matchId`, `fields[MAX_FIELDS]` with `fieldCount`, `code`, `rawCode`.
- `QuickdrawUploadResult`: `rejectionCount` and `isFullyAccepted()`. Rejections themselves are delivered through a `RejectionSink`.

*Why:* `Match` currently has no UUIDs, teams, winner, or timestamp, and stage 3 will change its storage format. Keeping the wire contract separate keeps this stage independent and testable now. Stage 4 adds a small `Match` → `QuickdrawMatchRecord` mapping.
*Alternative:* encode directly from `Match`. Rejected: it would force the `Match` changes into this stage.

### UUIDs are 16 bytes past the codec
Library types hold `UUIDBytes` (`lib/core/include/utils/uuid-bytes.hpp`), a 16-byte value with `parse`, `format`, `isNil`, and `==`.

`DevicePlayerResponse` and `DevicePlayerAffiliation` carry UUIDs as 16-byte `bytes` in network byte order. Their options are `max_size:16 fixed_length:true`, so nanopb fails the decode on any other length and the decoder copies the bytes straight into `UUIDBytes`. An absent player, category, or affiliation ID decodes as nil and fails the whole decode; an absent team ID means no team.

The `QuickdrawMatch` and `QuickdrawMatchRejection` messages still carry UUIDs as 36-character strings. They exist as text only inside the short-lived nanopb message struct. The decoder parses each UUID field and fails the whole decode on malformed text. The encoder formats lowercase text into the message just before encoding it. `static_assert`s tie `UUIDBytes::TEXT_BUFFER_SIZE` to the generated `max_size:37` fields and to `IdGenerator::UUID_BUFFER_SIZE`.

`parse` accepts 8-4-4-4-12 hex in either case and checks nothing else: no version, variant, or nil checks. It does not reuse `IdGenerator::uuidStringToBytes`, which maps non-hex characters to zero; that function and its callers are unchanged.

*Why:* 21 bytes saved per stored UUID, value comparison is a 16-byte `memcmp`, and a UUID can no longer be confused with a name.
*Trade-off:* one malformed UUID from the server makes the response undecodable.

### Server strings and lists decode into bounded buffers
The server sends display names as unterminated printable ASCII of up to 255 bytes, and sets no limit on the number of affiliations. In `device_api.options`, the player, team, category, and affiliation names are `FT_CALLBACK`. A single string callback reads at most 36 bytes into the `DevicePlayer` buffer, skips the rest, and terminates. `affiliations` is `FT_CALLBACK`: each submessage decodes into the vector until it holds `MAX_AFFILIATIONS`, and later submessages are skipped unread. Rejection `fields` is `max_count:2 max_size:24` (`SAME_PLAYER` names two fields).

*Why:* no heap for player strings, and a 37-byte buffer is enough for the display, so names are truncated rather than reserving 256 bytes each. A long name or an extra affiliation no longer makes the whole response undecodable.
*Trade-off:* truncated names may collide; identity is always by UUID. Skipped affiliations are not validated. A third rejection field still makes the rejection response undecodable.
*Alternative:* `FT_CALLBACK` into `std::string`/`std::vector`. Rejected: heap churn for strings that are bounded in practice.

### Rejections stream through a sink
`WriteQuickdrawMatchesResponse.rejected` stays `FT_CALLBACK`. `decodeResponse(body, onRejection, result)` decodes one static `QuickdrawMatchRejection` at a time and hands it to `onRejection`. The body is decoded twice: a validation pass that only counts, then a delivery pass, so the sink never sees rejections from a response that later fails to decode.

*Why:* up to 100 rejections would be ~10 KiB as a fixed array. Streaming mirrors how the request encoder pulls matches from a source.

### Request encoding streams matches from a caller-supplied source
`QuickdrawMatch` string fields use static `max_size:37`, since we generate the values and they are bounded. `WriteQuickdrawMatchesRequest.matches` is `FT_CALLBACK`. The encoder takes a count and a lambda, `std::function<bool(size_t index, QuickdrawMatchRecord& record)>`, and encodes one record at a time into a `pb_ostream_t` whose callback appends to the `std::string` payload. The return value is `bool`; `false` means encoding failed or the source failed.

*Why:* a full batch of 100 `QuickdrawMatch` structs is ~20 KiB, which must not land on the stack. Streaming means only one record exists at a time, and the payload is built in a single pass. The source lambda is the repo's callback idiom and fits how stage 4 will read from `MatchManager`.
*Alternative:* `const std::vector<QuickdrawMatchRecord>&`. Simpler, but the caller has to materialize every record first. A follow-up can add a vector overload if useful.
*Alternative:* `pb_get_encoded_size` followed by an exact-size encode. Rejected because it walks the source twice.

### Acceptance is inferred from the response body
An empty success body means "all accepted": the server returns 204, and an empty `WriteQuickdrawMatchesResponse` also encodes to zero bytes. A non-empty body is decoded, and if decoding fails the client calls the error callback, so the caller keeps the whole batch. Retrying is safe because the server treats the first accepted report as authoritative.

*Why:* the status code isn't available, and the body alone is unambiguous for this endpoint.

### Unknown error codes are kept, not dropped
`QuickdrawMatchErrorCode` is a library `enum class` with `UNKNOWN` plus the six known values. The decoder copies the raw `int32` into `rawCode` and maps unknown values to `UNKNOWN`. Upstream says clients must handle future codes.

### API surface sits next to the legacy calls
- `PlayerApi::getDevicePlayerByPin(wirelessManager, pin, onSuccess(const DevicePlayer&), onError)`
- `MatchesApi::uploadQuickdrawMatches(wirelessManager, count, source, onRejection(const QuickdrawMatchRejection&), onSuccess(const QuickdrawUploadResult&), onError)`

The codec logic lives in `device-player-decoder.{hpp,cpp}` and `quickdraw-match-codec.{hpp,cpp}`, mirroring `crash-encoder`. The PIN is passed through unvalidated: it comes from rigid on-device input, and the server answers a malformed one with 400. Validation failures (batch out of range, encode failure) call `onError` with `INVALID_STATE` synchronously and queue nothing, which matches `CrashApi`'s encode-failure path.

## Risks / Trade-offs

- [The vendored proto drifts from upstream again] → `upstream.lock.json` records the commit. The regeneration task diffs field numbers against upstream before committing.
- [Changing `.options` for new messages alters generated code for `WriteDeviceLogRequest`] → leave its options unchanged and keep the existing crash-encoder tests as a regression guard.
- [The 404 for an unknown PIN can't be told apart from other client errors] → accepted for this stage. Callers treat every lookup error the way they do today (offline path). A later stage can add a status code to `WirelessErrorInfo`.
- [The server sends a UUID field that is not UUID-shaped] → the whole decode fails and the caller takes its error path. The server only issues UUIDs for these fields.
- [A server value exceeds a static limit and the whole decode fails] → limits are part of the device-capability API version; a server change that needs larger limits ships with a new capability version and regenerated options.
- [Stack use during player decode] → the generated message and the `DevicePlayer` copy are each under 1 KiB and live together on the HTTP callback task's stack.
- [The source lambda returns inconsistent data across calls] → the encoder calls it exactly once per index, in order.

## Migration Plan

This is additive only, with no firmware behavior change and no rollback concern. Later stages: merge `http-date-clock-sync` and add `Player` fields, then `Match`/packet format, then migrating callers and deleting `PlayerResponse`, `PlayerApi::getPlayer`, and `MatchesApi::updateMatches`.

## Open Questions

- How hunter role and allegiance map onto affiliations (by category name, e.g. "Allegiance"). This affects stage 4's caller mapping, not this library.
- Whether `library.json` should declare Nanopb explicitly. It already resolves through `platformio.ini` `lib_deps` for both firmware and native builds.
