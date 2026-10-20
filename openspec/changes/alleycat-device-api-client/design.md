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
- Bounded stack use on ESP32-S3, with no fixed buffers sized for worst-case names or batch sizes.

**Non-Goals:**
- Migrating `FetchUserDataState` or `UploadMatchesState`, and removing the legacy JSON clients.
- Changes to `Player`, `Match`, `MatchManager`, `QuickdrawPacket`, or persisted storage.
- Producing `occurred_at_unix_ms`. The caller supplies the value, from `PlatformClock::unixMilliseconds()` added by the `http-date-clock-sync` branch.
- Splitting stored matches into batches of 100. The caller does this.
- Passing HTTP status codes through `WirelessErrorInfo`. This would change `lib/core` and both HTTP drivers.
- Client-side UUID validation. The server reports invalid entries one by one.

## Decisions

### Library-owned types instead of `Player` / `Match`
The new API takes and returns types defined in `lib/alleycat-server`:
- `DevicePlayer`: `id`, `eventId`, `pdnCode`, `name`, `createdAtUnixMs`, `active`, `teamId`/`teamName` with `hasTeam()`, and `std::vector<DevicePlayerAffiliation> affiliations`.
- `QuickdrawMatchRecord`: fixed `char[IdGenerator::UUID_BUFFER_SIZE]` fields for match, player A/B, winner, and team A/B (an empty string means no team), plus `uint64_t` draw times and `int64_t occurredAtUnixMs`.
- `QuickdrawMatchRejection`: `matchIndex`, `matchId`, `fields`, `code`, `rawCode`.
- `QuickdrawUploadResult`: `std::vector<QuickdrawMatchRejection> rejections` and `isFullyAccepted()`.

*Why:* `Match` currently has no UUIDs, teams, winner, or timestamp, and stage 3 will change its storage format. Keeping the wire contract separate keeps this stage independent and testable now. Stage 4 adds a small `Match` → `QuickdrawMatchRecord` mapping.
*Alternative:* encode directly from `Match`. Rejected: it would force the `Match` changes into this stage.

### Strings decode through nanopb callbacks into `std::string`
In `device_api.options`, every string in `DevicePlayerResponse`, `DevicePlayerAffiliation`, and `QuickdrawMatchRejection`, and every repeated field in those messages, is marked `type:FT_CALLBACK`. The decoder appends into `std::string` and `std::vector`.

*Why:* the server allows names up to 256 graphemes (up to ~1 KiB of UTF-8), and affiliation and rejection counts are not small fixed numbers. Static `max_size`/`max_count` buffers would cost several KiB of stack per decode, or would truncate and fail on long names.
*Alternative:* static buffers with truncation. Rejected because nanopb fails the whole decode when a static field overflows.

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
- `MatchesApi::uploadQuickdrawMatches(wirelessManager, count, source, onSuccess(const QuickdrawUploadResult&), onError)`

The codec logic lives in `device-player-decoder.{hpp,cpp}` and `quickdraw-match-codec.{hpp,cpp}`, mirroring `crash-encoder`. The PIN is passed through unvalidated: it comes from rigid on-device input, and the server answers a malformed one with 400. Validation failures (batch out of range, encode failure) call `onError` with `INVALID_STATE` synchronously and queue nothing, which matches `CrashApi`'s encode-failure path.

## Risks / Trade-offs

- [The vendored proto drifts from upstream again] → `upstream.lock.json` records the commit. The regeneration task diffs field numbers against upstream before committing.
- [Changing `.options` for new messages alters generated code for `WriteDeviceLogRequest`] → leave its options unchanged and keep the existing crash-encoder tests as a regression guard.
- [The 404 for an unknown PIN can't be told apart from other client errors] → accepted for this stage. Callers treat every lookup error the way they do today (offline path). A later stage can add a status code to `WirelessErrorInfo`.
- [Heap churn from `std::string`/`std::vector` during decode] → only on one-off lookups and upload responses, not in game loops.
- [The source lambda returns inconsistent data across calls] → the encoder calls it exactly once per index, in order.

## Migration Plan

This is additive only, with no firmware behavior change and no rollback concern. Later stages: merge `http-date-clock-sync` and add `Player` fields, then `Match`/packet format, then migrating callers and deleting `PlayerResponse`, `PlayerApi::getPlayer`, and `MatchesApi::updateMatches`.

## Open Questions

- How hunter role and allegiance map onto affiliations (by category name, e.g. "Allegiance"). This affects stage 4's caller mapping, not this library.
- Whether `library.json` should declare Nanopb explicitly. It already resolves through `platformio.ini` `lib_deps` for both firmware and native builds.
