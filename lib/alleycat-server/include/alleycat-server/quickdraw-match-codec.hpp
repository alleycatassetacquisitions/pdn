#pragma once

#include "id-generator.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * One completed match for POST /quickdraw-matches. All IDs are UUID strings; an
 * empty team ID means that player was unassigned at match time.
 */
struct QuickdrawMatchRecord {
    char matchId[IdGenerator::UUID_BUFFER_SIZE] = {};
    char playerAId[IdGenerator::UUID_BUFFER_SIZE] = {};
    char playerBId[IdGenerator::UUID_BUFFER_SIZE] = {};
    char playerATeamId[IdGenerator::UUID_BUFFER_SIZE] = {};
    char playerBTeamId[IdGenerator::UUID_BUFFER_SIZE] = {};
    char winnerPlayerId[IdGenerator::UUID_BUFFER_SIZE] = {};
    uint64_t playerADrawTimeMs = 0;
    uint64_t playerBDrawTimeMs = 0;
    int64_t occurredAtUnixMs = 0;
};

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

/** First error the server found in one submitted match. */
struct QuickdrawMatchRejection {
    uint32_t matchIndex = 0;
    std::string matchId;
    std::vector<std::string> fields;
    QuickdrawMatchErrorCode code = QuickdrawMatchErrorCode::UNKNOWN;
    int32_t rawCode = 0;
};

/** Outcome of an upload. Matches not listed in @ref rejections were saved or already stored. */
struct QuickdrawUploadResult {
    std::vector<QuickdrawMatchRejection> rejections;

    /** @return true when every match in the batch was accepted. */
    bool isFullyAccepted() const {
        return rejections.empty();
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

/** Fills @p record with the match at @p index; returns false to abort encoding. */
using MatchSource = std::function<bool(size_t index, QuickdrawMatchRecord& record)>;

/**
 * Encodes @p matchCount matches pulled from @p source, in index order, into @p payload.
 * @p source is called exactly once per index.
 *
 * @return false when @p source fails or encoding fails.
 */
bool encodeRequest(size_t matchCount, const MatchSource& source, std::string& payload);

/**
 * Decodes an upload response body into @p result. An empty body (HTTP 204) means
 * every match was accepted.
 *
 * @return false when @p body is not a valid WriteQuickdrawMatchesResponse.
 */
bool decodeResponse(const std::string& body, QuickdrawUploadResult& result);

}  // namespace QuickdrawMatchCodec
