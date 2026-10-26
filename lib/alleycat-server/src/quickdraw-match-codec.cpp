#include "alleycat-server/quickdraw-match-codec.hpp"
#include "alleycat-server/device_api.pb.h"
#include "id-generator.hpp"
#include <pb_decode.h>
#include <pb_encode.h>
#include <cstdint>
#include <cstring>

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
    record.matchId.format(message.id);
    record.playerAId.format(message.player_a_id);
    record.playerBId.format(message.player_b_id);
    record.winnerPlayerId.format(message.winner_player_id);

    if (!record.playerATeamId.isZero()) {
        record.playerATeamId.format(message.player_a_team_id);
        message.has_player_a_team_id = true;
    }
    if (!record.playerBTeamId.isZero()) {
        record.playerBTeamId.format(message.player_b_team_id);
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

using RejectionMessage = alleycat_device_QuickdrawMatchRejection;

static_assert(sizeof(RejectionMessage::match_id) == UUIDBytes::TEXT_BUFFER_SIZE,
              "UUID fields in device_api.options must hold UUIDBytes text");
static_assert(UUIDBytes::TEXT_BUFFER_SIZE == IdGenerator::UUID_BUFFER_SIZE,
              "UUIDBytes text must match IdGenerator's UUID strings");
static_assert(sizeof(QuickdrawMatchRejection::fields) == sizeof(RejectionMessage::fields),
              "QuickdrawMatchRejection must match device_api.options");

/** One pass over the rejections; a null sink only validates and counts. */
struct RejectionPass {
    const QuickdrawMatchCodec::RejectionSink* onRejection;
    size_t rejectionCount;
};

bool decodeRejection(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    RejectionPass* pass = static_cast<RejectionPass*>(*arg);

    RejectionMessage message = alleycat_device_QuickdrawMatchRejection_init_zero;
    if (!pb_decode(stream, alleycat_device_QuickdrawMatchRejection_fields, &message)) {
        return false;
    }
    // Parsed before the validation pass returns, so a malformed ID fails the whole
    // body before anything reaches the sink.
    UUIDBytes matchId;
    if (!UUIDBytes::parse(message.match_id, matchId)) {
        return false;
    }
    pass->rejectionCount++;
    if (pass->onRejection == nullptr) {
        return true;
    }

    QuickdrawMatchRejection rejection;
    rejection.matchIndex = message.match_index;
    rejection.matchId = matchId;
    rejection.fieldCount = message.fields_count;
    std::memcpy(rejection.fields, message.fields, sizeof(rejection.fields));
    rejection.rawCode = static_cast<int32_t>(message.code);
    rejection.code = toErrorCode(rejection.rawCode);
    (*pass->onRejection)(rejection);
    return true;
}

bool decodeRejections(const std::string& body, RejectionPass& pass) {
    alleycat_device_WriteQuickdrawMatchesResponse message = alleycat_device_WriteQuickdrawMatchesResponse_init_zero;
    message.rejected.funcs.decode = &decodeRejection;
    message.rejected.arg = &pass;

    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
    return pb_decode(&stream, alleycat_device_WriteQuickdrawMatchesResponse_fields, &message);
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

bool decodeResponse(const std::string& body, const RejectionSink& onRejection, QuickdrawUploadResult& result) {
    result = QuickdrawUploadResult();
    if (body.empty()) {
        return true;
    }

    // Validate the whole body before delivering anything, so a truncated response
    // never hands the caller a partial set of rejections.
    RejectionPass validation = {nullptr, 0};
    if (!decodeRejections(body, validation)) {
        return false;
    }
    RejectionPass delivery = {&onRejection, 0};
    if (!decodeRejections(body, delivery)) {
        return false;
    }
    result.rejectionCount = delivery.rejectionCount;
    return true;
}

}  // namespace QuickdrawMatchCodec
