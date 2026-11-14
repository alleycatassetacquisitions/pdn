#pragma once

#include "alleycat-server/fdn-stored-entry.hpp"
#include "utils/uuid-bytes.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

/** Reason the server rejected one match. UNKNOWN covers codes added after this build. */
enum class QuickdrawMatchErrorCode {
    UNKNOWN,
    INVALID_UUID,
    MISSING_FIELD,
    OUT_OF_RANGE,
    SAME_PLAYER,
    INVALID_WINNER,
    INVALID_EVENT_REFERENCE
};

/**
 * First error the server found in one submitted match. Sizes must match
 * device_api.options; @ref fields names the QuickdrawMatch fields involved.
 */
struct QuickdrawMatchRejection {
    static constexpr size_t MAX_FIELDS = 2;
    static constexpr size_t FIELD_NAME_BUFFER_SIZE = 24;

    uint32_t matchIndex = 0;
    UUIDBytes matchId;
    char fields[MAX_FIELDS][FIELD_NAME_BUFFER_SIZE] = {};
    size_t fieldCount = 0;
    QuickdrawMatchErrorCode code = QuickdrawMatchErrorCode::UNKNOWN;
    int32_t rawCode = 0;
};

/** Outcome of an upload. Matches that were not rejected were saved or already stored. */
struct QuickdrawUploadResult {
    size_t rejectionCount = 0;

    /** @return true when every match in the batch was accepted. */
    bool isFullyAccepted() const {
        return rejectionCount == 0;
    }
};

/**
 * Encodes WriteQuickdrawMatchesRequest and decodes WriteQuickdrawMatchesResponse.
 *
 * Pure functions with no platform dependencies — testable on native.
 */
namespace QuickdrawMatchCodec {

/** Server limit on matches per request. */
constexpr size_t MAX_MATCHES_PER_BATCH = 100;

/** Fills @p entry with the stored match record at @p index; returns false to abort encoding. */
using MatchSource = std::function<bool(size_t index, FdnStoredMatchEntry& entry)>;

/** Receives each rejection, in request order. */
using RejectionSink = std::function<void(const QuickdrawMatchRejection& rejection)>;

/**
 * Encodes @p matchCount matches pulled from @p source, in index order, into @p payload.
 * @p source is called exactly once per index. Each match's fields are decoded
 * from its entry's record bytes; a nil team ID means that player was
 * unassigned at match time, and the field is left out.
 *
 * @return false when @p source fails or encoding fails.
 */
bool encodeRequest(size_t matchCount, const MatchSource& source, std::string& payload);

/**
 * Decodes an upload response body, passing each rejection to @p onRejection and
 * the totals to @p result. An empty body (HTTP 204) means every match was accepted.
 * @p onRejection is only called once the whole body has decoded successfully.
 *
 * @return false when @p body is not a valid WriteQuickdrawMatchesResponse.
 */
bool decodeResponse(const std::string& body, const RejectionSink& onRejection, QuickdrawUploadResult& result);

}  // namespace QuickdrawMatchCodec
