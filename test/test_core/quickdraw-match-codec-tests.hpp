#pragma once

#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/matches-api.hpp"
#include "alleycat-server/quickdraw-match-codec.hpp"

inline QuickdrawMatchRecord makeMatchRecord(size_t index) {
    QuickdrawMatchRecord record;
    snprintf(record.matchId, sizeof(record.matchId), "00000000-0000-4000-8000-%012zu", index);
    IdGenerator::copyId(record.playerAId, "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    IdGenerator::copyId(record.playerBId, "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    IdGenerator::copyId(record.winnerPlayerId, record.playerAId);
    record.playerADrawTimeMs = 210;
    record.playerBDrawTimeMs = 340;
    record.occurredAtUnixMs = 1792359000000;
    return record;
}

inline QuickdrawMatchCodec::MatchSource makeMatchSource(const std::vector<QuickdrawMatchRecord>& records) {
    return [&records](size_t index, QuickdrawMatchRecord& record) {
        record = records[index];
        return true;
    };
}

inline bool decodeTestMatch(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    std::vector<alleycat_device_QuickdrawMatch>* matches =
        static_cast<std::vector<alleycat_device_QuickdrawMatch>*>(*arg);
    alleycat_device_QuickdrawMatch message = alleycat_device_QuickdrawMatch_init_zero;
    if (!pb_decode(stream, alleycat_device_QuickdrawMatch_fields, &message)) {
        return false;
    }
    matches->push_back(message);
    return true;
}

inline std::vector<alleycat_device_QuickdrawMatch> decodeTestRequest(const std::string& payload) {
    std::vector<alleycat_device_QuickdrawMatch> matches;
    alleycat_device_WriteQuickdrawMatchesRequest message = alleycat_device_WriteQuickdrawMatchesRequest_init_zero;
    message.matches.funcs.decode = &decodeTestMatch;
    message.matches.arg = &matches;
    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(payload.data()), payload.size());
    EXPECT_TRUE(pb_decode(&stream, alleycat_device_WriteQuickdrawMatchesRequest_fields, &message));
    return matches;
}

inline alleycat_device_QuickdrawMatchRejection makeRejectionMessage(
    uint32_t matchIndex, const char* matchId, int32_t rawCode) {
    alleycat_device_QuickdrawMatchRejection message = alleycat_device_QuickdrawMatchRejection_init_zero;
    message.match_index = matchIndex;
    IdGenerator::copyId(message.match_id, matchId);
    message.code = static_cast<alleycat_device_QuickdrawMatchErrorCode>(rawCode);
    return message;
}

inline bool encodeTestRejections(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const std::vector<alleycat_device_QuickdrawMatchRejection>* rejections =
        static_cast<const std::vector<alleycat_device_QuickdrawMatchRejection>*>(*arg);
    for (const alleycat_device_QuickdrawMatchRejection& message : *rejections) {
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, alleycat_device_QuickdrawMatchRejection_fields, &message)) {
            return false;
        }
    }
    return true;
}

inline std::string encodeUploadResponse(const std::vector<alleycat_device_QuickdrawMatchRejection>& rejections) {
    alleycat_device_WriteQuickdrawMatchesResponse message = alleycat_device_WriteQuickdrawMatchesResponse_init_zero;
    message.rejected.funcs.encode = &encodeTestRejections;
    message.rejected.arg = const_cast<std::vector<alleycat_device_QuickdrawMatchRejection>*>(&rejections);
    return encodeTestMessage(alleycat_device_WriteQuickdrawMatchesResponse_fields, &message);
}

inline QuickdrawMatchCodec::RejectionSink collectRejections(std::vector<QuickdrawMatchRejection>& rejections) {
    return [&rejections](const QuickdrawMatchRejection& rejection) {
        rejections.push_back(rejection);
    };
}

// ---- Request encoding ----

inline void matchCodecRoundTripsAllFields() {
    std::vector<QuickdrawMatchRecord> records = {makeMatchRecord(0), makeMatchRecord(1)};
    IdGenerator::copyId(records[1].playerATeamId, "11111111-1111-4111-8111-111111111111");
    IdGenerator::copyId(records[1].playerBTeamId, "22222222-2222-4222-8222-222222222222");
    IdGenerator::copyId(records[1].winnerPlayerId, records[1].playerBId);

    std::string payload;
    ASSERT_TRUE(QuickdrawMatchCodec::encodeRequest(records.size(), makeMatchSource(records), payload));

    std::vector<alleycat_device_QuickdrawMatch> matches = decodeTestRequest(payload);
    ASSERT_EQ(matches.size(), 2u);
    const alleycat_device_QuickdrawMatch& match = matches[1];
    EXPECT_STREQ(match.id, records[1].matchId);
    EXPECT_STREQ(match.player_a_id, records[1].playerAId);
    EXPECT_STREQ(match.player_b_id, records[1].playerBId);
    EXPECT_STREQ(match.winner_player_id, records[1].playerBId);
    EXPECT_TRUE(match.has_player_a_team_id);
    EXPECT_STREQ(match.player_a_team_id, records[1].playerATeamId);
    EXPECT_TRUE(match.has_player_b_team_id);
    EXPECT_STREQ(match.player_b_team_id, records[1].playerBTeamId);
    EXPECT_TRUE(match.has_player_a_draw_time_ms);
    EXPECT_EQ(match.player_a_draw_time_ms, 210u);
    EXPECT_TRUE(match.has_player_b_draw_time_ms);
    EXPECT_EQ(match.player_b_draw_time_ms, 340u);
    EXPECT_TRUE(match.has_occurred_at_unix_ms);
    EXPECT_EQ(match.occurred_at_unix_ms, 1792359000000);
    EXPECT_STREQ(matches[0].id, records[0].matchId);
}

inline void matchCodecKeepsZeroDrawTimePresent() {
    std::vector<QuickdrawMatchRecord> records = {makeMatchRecord(0)};
    records[0].playerADrawTimeMs = 0;

    std::string payload;
    ASSERT_TRUE(QuickdrawMatchCodec::encodeRequest(records.size(), makeMatchSource(records), payload));

    std::vector<alleycat_device_QuickdrawMatch> matches = decodeTestRequest(payload);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(matches[0].has_player_a_draw_time_ms);
    EXPECT_EQ(matches[0].player_a_draw_time_ms, 0u);
}

inline void matchCodecOmitsUnassignedTeams() {
    std::vector<QuickdrawMatchRecord> records = {makeMatchRecord(0)};

    std::string payload;
    ASSERT_TRUE(QuickdrawMatchCodec::encodeRequest(records.size(), makeMatchSource(records), payload));

    std::vector<alleycat_device_QuickdrawMatch> matches = decodeTestRequest(payload);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_FALSE(matches[0].has_player_a_team_id);
    EXPECT_FALSE(matches[0].has_player_b_team_id);
}

inline void matchCodecFailsWhenSourceFails() {
    std::string payload;
    QuickdrawMatchCodec::MatchSource failingSource = [](size_t index, QuickdrawMatchRecord& record) {
        record = makeMatchRecord(index);
        return index == 0;
    };

    EXPECT_FALSE(QuickdrawMatchCodec::encodeRequest(2, failingSource, payload));
}

// ---- Response decoding ----

inline void matchCodecTreatsEmptyBodyAsFullyAccepted() {
    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    ASSERT_TRUE(QuickdrawMatchCodec::decodeResponse("", collectRejections(rejections), result));

    EXPECT_TRUE(result.isFullyAccepted());
    EXPECT_TRUE(rejections.empty());
}

inline void matchCodecDecodesRejection() {
    alleycat_device_QuickdrawMatchRejection source = makeRejectionMessage(
        2,
        "00000000-0000-4000-8000-000000000002",
        alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_SAME_PLAYER);
    source.fields_count = 2;
    IdGenerator::copyId(source.fields[0], "player_a_id");
    IdGenerator::copyId(source.fields[1], "player_b_id");

    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    ASSERT_TRUE(QuickdrawMatchCodec::decodeResponse(encodeUploadResponse({source}), collectRejections(rejections), result));

    EXPECT_FALSE(result.isFullyAccepted());
    EXPECT_EQ(result.rejectionCount, 1u);
    ASSERT_EQ(rejections.size(), 1u);
    const QuickdrawMatchRejection& rejection = rejections[0];
    EXPECT_EQ(rejection.matchIndex, 2u);
    EXPECT_STREQ(rejection.matchId, source.match_id);
    ASSERT_EQ(rejection.fieldCount, 2u);
    EXPECT_STREQ(rejection.fields[0], "player_a_id");
    EXPECT_STREQ(rejection.fields[1], "player_b_id");
    EXPECT_EQ(rejection.code, QuickdrawMatchErrorCode::SAME_PLAYER);
}

inline void matchCodecDeliversRejectionsInOrder() {
    std::vector<alleycat_device_QuickdrawMatchRejection> sources;
    for (uint32_t index = 0; index < QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH; index++) {
        sources.push_back(makeRejectionMessage(
            index,
            "00000000-0000-4000-8000-000000000000",
            alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_UUID));
    }

    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    ASSERT_TRUE(QuickdrawMatchCodec::decodeResponse(encodeUploadResponse(sources), collectRejections(rejections), result));

    EXPECT_EQ(result.rejectionCount, QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH);
    ASSERT_EQ(rejections.size(), QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH);
    for (uint32_t index = 0; index < QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH; index++) {
        EXPECT_EQ(rejections[index].matchIndex, index);
    }
}

inline void matchCodecKeepsUnknownErrorCode() {
    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    ASSERT_TRUE(QuickdrawMatchCodec::decodeResponse(
        encodeUploadResponse({makeRejectionMessage(0, "00000000-0000-4000-8000-000000000000", 42)}),
        collectRejections(rejections),
        result));

    ASSERT_EQ(rejections.size(), 1u);
    EXPECT_EQ(rejections[0].code, QuickdrawMatchErrorCode::UNKNOWN);
    EXPECT_EQ(rejections[0].rawCode, 42);
}

inline void matchCodecRejectsTooManyFieldsWithoutDelivering() {
    alleycat_device_QuickdrawMatchRejection valid = makeRejectionMessage(
        0,
        "00000000-0000-4000-8000-000000000000",
        alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_SAME_PLAYER);
    valid.fields_count = QuickdrawMatchRejection::MAX_FIELDS;
    IdGenerator::copyId(valid.fields[0], "player_a_id");
    IdGenerator::copyId(valid.fields[1], "player_b_id");
    std::string overflowing = encodeTestMessage(alleycat_device_QuickdrawMatchRejection_fields, &valid);
    appendTestBytesField(overflowing, alleycat_device_QuickdrawMatchRejection_fields_tag, "winner_player_id");

    std::string body = encodeUploadResponse({valid});
    appendTestBytesField(body, alleycat_device_WriteQuickdrawMatchesResponse_rejected_tag, overflowing);

    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    EXPECT_FALSE(QuickdrawMatchCodec::decodeResponse(body, collectRejections(rejections), result));
    EXPECT_TRUE(rejections.empty());
}

inline void matchCodecRejectsGarbageBody() {
    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    EXPECT_FALSE(QuickdrawMatchCodec::decodeResponse("not a protobuf", collectRejections(rejections), result));
}

// ---- Upload API ----

inline void matchesApiRejectsOutOfRangeCounts(AlleycatApiTestSuite* suite) {
    std::vector<QuickdrawMatchRecord> records = {makeMatchRecord(0)};
    for (size_t matchCount : {static_cast<size_t>(0), QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH + 1}) {
        bool errorReported = false;
        WirelessError errorCode = WirelessError::TIMEOUT;
        MatchesApi::uploadQuickdrawMatches(
            suite->wirelessManager,
            matchCount,
            makeMatchSource(records),
            [](const QuickdrawMatchRejection&) {},
            [](const QuickdrawUploadResult&) {},
            [&errorReported, &errorCode](const WirelessErrorInfo& error) {
                errorReported = true;
                errorCode = error.code;
            });

        EXPECT_TRUE(errorReported) << "matchCount=" << matchCount;
        EXPECT_EQ(errorCode, WirelessError::INVALID_STATE) << "matchCount=" << matchCount;
    }
    EXPECT_TRUE(suite->queuedRequests.empty());
}

inline void matchesApiQueuesProtobufPost(AlleycatApiTestSuite* suite) {
    std::vector<QuickdrawMatchRecord> records = {makeMatchRecord(0)};
    MatchesApi::uploadQuickdrawMatches(
        suite->wirelessManager,
        records.size(),
        makeMatchSource(records),
        [](const QuickdrawMatchRejection&) {},
        [](const QuickdrawUploadResult&) {},
        [](const WirelessErrorInfo&) {});

    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    const HttpRequest& request = suite->queuedRequests[0];
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.path, "/quickdraw-matches");
    EXPECT_EQ(request.contentType, "application/protobuf");
    EXPECT_EQ(decodeTestRequest(request.payload).size(), 1u);
}
