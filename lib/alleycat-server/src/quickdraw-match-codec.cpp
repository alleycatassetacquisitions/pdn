#include "alleycat-server/quickdraw-match-codec.hpp"
#include "alleycat-server/device_api.pb.h"
#include "nanopb-string-callbacks.hpp"
#include <pb_decode.h>
#include <pb_encode.h>
#include <cstdint>
#include <utility>

namespace {

struct MatchBatch {
    size_t matchCount;
    const QuickdrawMatchCodec::MatchSource* source;
};

bool appendToPayload(pb_ostream_t* stream, const pb_byte_t* buffer, size_t count) {
    std::string* payload = static_cast<std::string*>(stream->state);
    payload->append(reinterpret_cast<const char*>(buffer), count);
    return true;
}

alleycat_device_QuickdrawMatch toMessage(const QuickdrawMatchRecord& record) {
    alleycat_device_QuickdrawMatch message = alleycat_device_QuickdrawMatch_init_zero;
    IdGenerator::copyId(message.id, record.matchId);
    IdGenerator::copyId(message.player_a_id, record.playerAId);
    IdGenerator::copyId(message.player_b_id, record.playerBId);
    IdGenerator::copyId(message.winner_player_id, record.winnerPlayerId);

    if (record.playerATeamId[0] != '\0') {
        IdGenerator::copyId(message.player_a_team_id, record.playerATeamId);
        message.has_player_a_team_id = true;
    }
    if (record.playerBTeamId[0] != '\0') {
        IdGenerator::copyId(message.player_b_team_id, record.playerBTeamId);
        message.has_player_b_team_id = true;
    }

    // Zero is a valid measurement, but the server rejects an absent one.
    message.has_player_a_draw_time_ms = true;
    message.player_a_draw_time_ms = record.playerADrawTimeMs;
    message.has_player_b_draw_time_ms = true;
    message.player_b_draw_time_ms = record.playerBDrawTimeMs;
    message.has_occurred_at_unix_ms = true;
    message.occurred_at_unix_ms = record.occurredAtUnixMs;
    return message;
}

bool encodeMatches(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const MatchBatch* batch = static_cast<const MatchBatch*>(*arg);
    for (size_t index = 0; index < batch->matchCount; index++) {
        QuickdrawMatchRecord record;
        if (!(*batch->source)(index, record)) {
            return false;
        }
        const alleycat_device_QuickdrawMatch message = toMessage(record);
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, alleycat_device_QuickdrawMatch_fields, &message)) {
            return false;
        }
    }
    return true;
}

QuickdrawMatchErrorCode toErrorCode(int32_t rawCode) {
    switch (rawCode) {
        case alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_UUID:
            return QuickdrawMatchErrorCode::INVALID_UUID;
        case alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_MISSING_FIELD:
            return QuickdrawMatchErrorCode::MISSING_FIELD;
        case alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_OUT_OF_RANGE:
            return QuickdrawMatchErrorCode::OUT_OF_RANGE;
        case alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_SAME_PLAYER:
            return QuickdrawMatchErrorCode::SAME_PLAYER;
        case alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_WINNER:
            return QuickdrawMatchErrorCode::INVALID_WINNER;
        case alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_EVENT_REFERENCE:
            return QuickdrawMatchErrorCode::INVALID_EVENT_REFERENCE;
        default:
            return QuickdrawMatchErrorCode::UNKNOWN;
    }
}

bool decodeRejection(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    std::vector<QuickdrawMatchRejection>* rejections =
        static_cast<std::vector<QuickdrawMatchRejection>*>(*arg);

    QuickdrawMatchRejection rejection;
    alleycat_device_QuickdrawMatchRejection message = alleycat_device_QuickdrawMatchRejection_init_zero;
    NanopbStringCallbacks::bindString(message.match_id, rejection.matchId);
    message.fields.funcs.decode = &NanopbStringCallbacks::appendString;
    message.fields.arg = &rejection.fields;

    if (!pb_decode(stream, alleycat_device_QuickdrawMatchRejection_fields, &message)) {
        return false;
    }
    rejection.matchIndex = message.match_index;
    rejection.rawCode = static_cast<int32_t>(message.code);
    rejection.code = toErrorCode(rejection.rawCode);
    rejections->push_back(std::move(rejection));
    return true;
}

}  // namespace

namespace QuickdrawMatchCodec {

bool encodeRequest(size_t matchCount, const MatchSource& source, std::string& payload) {
    payload.clear();

    MatchBatch batch = {matchCount, &source};
    alleycat_device_WriteQuickdrawMatchesRequest message = alleycat_device_WriteQuickdrawMatchesRequest_init_zero;
    message.matches.funcs.encode = &encodeMatches;
    message.matches.arg = &batch;

    pb_ostream_t stream = {};
    stream.callback = &appendToPayload;
    stream.state = &payload;
    stream.max_size = SIZE_MAX;
    return pb_encode(&stream, alleycat_device_WriteQuickdrawMatchesRequest_fields, &message);
}

bool decodeResponse(const std::string& body, QuickdrawUploadResult& result) {
    result = QuickdrawUploadResult();
    if (body.empty()) {
        return true;
    }

    alleycat_device_WriteQuickdrawMatchesResponse message = alleycat_device_WriteQuickdrawMatchesResponse_init_zero;
    message.rejected.funcs.decode = &decodeRejection;
    message.rejected.arg = &result.rejections;

    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
    return pb_decode(&stream, alleycat_device_WriteQuickdrawMatchesResponse_fields, &message);
}

}  // namespace QuickdrawMatchCodec
