#pragma once

#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/fdn-stored-entry.hpp"
#include "alleycat-server/matches-api.hpp"
#include "alleycat-server/quickdraw-match-codec.hpp"
#include "game/match.hpp"
#include "id-generator.hpp"
#include "uuid-bytes-tests.hpp"

inline Match makeTestMatch(size_t index) {
    char matchId[UUIDBytes::TEXT_BUFFER_SIZE];
    snprintf(matchId, sizeof(matchId), "00000000-0000-4000-8000-%012zu", index);
    Match match(matchId);
    match.setHunterPlayerId(makeTestUuid("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    match.setBountyPlayerId(makeTestUuid("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"));
    match.setWinnerPlayerId(match.getHunterPlayerId());
    match.setHunterDrawTime(210);
    match.setBountyDrawTime(340);
    match.setOccurredAtUnixMs(1792359000000);
    return match;
}

/** Hands out each of @p matches as the stored entry of a match record a PDN relayed. */
inline QuickdrawMatchCodec::MatchSource makeMatchSource(const std::vector<Match>& matches) {
    return [&matches](size_t index, FdnStoredMatchEntry& entry) {
        entry.kind = static_cast<uint8_t>(RecordKind::MATCH);
        entry.recordNumber = static_cast<uint32_t>(index + 1);
        matches[index].serialize(entry.content.recordBytes);
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
    std::vector<Match> records = {makeTestMatch(0), makeTestMatch(1)};
    records[1].setHunterTeamId(makeTestUuid("1111AAAA-1111-4111-8111-111111111111"));
    records[1].setBountyTeamId(makeTestUuid("22222222-2222-4222-8222-222222222222"));
    records[1].setWinnerPlayerId(records[1].getBountyPlayerId());

    std::string payload;
    ASSERT_TRUE(QuickdrawMatchCodec::encodeRequest(records.size(), makeMatchSource(records), payload));

    std::vector<alleycat_device_QuickdrawMatch> matches = decodeTestRequest(payload);
    ASSERT_EQ(matches.size(), 2u);
    const alleycat_device_QuickdrawMatch& match = matches[1];
    EXPECT_STREQ(match.id, "00000000-0000-4000-8000-000000000001");
    EXPECT_STREQ(match.player_a_id, "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    EXPECT_STREQ(match.player_b_id, "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    EXPECT_STREQ(match.winner_player_id, "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    EXPECT_TRUE(match.has_player_a_team_id);
    EXPECT_STREQ(match.player_a_team_id, "1111aaaa-1111-4111-8111-111111111111")
    << "codec writes lowercase";
    EXPECT_TRUE(match.has_player_b_team_id);
    EXPECT_STREQ(match.player_b_team_id, "22222222-2222-4222-8222-222222222222");
    EXPECT_TRUE(match.has_player_a_draw_time_ms);
    EXPECT_EQ(match.player_a_draw_time_ms, 210u);
    EXPECT_TRUE(match.has_player_b_draw_time_ms);
    EXPECT_EQ(match.player_b_draw_time_ms, 340u);
    EXPECT_TRUE(match.has_occurred_at_unix_ms);
    EXPECT_EQ(match.occurred_at_unix_ms, 1792359000000);
    EXPECT_STREQ(matches[0].id, "00000000-0000-4000-8000-000000000000");
}

inline void matchCodecKeepsZeroDrawTimePresent() {
    std::vector<Match> records = {makeTestMatch(0)};
    records[0].setHunterDrawTime(0);

    std::string payload;
    ASSERT_TRUE(QuickdrawMatchCodec::encodeRequest(records.size(), makeMatchSource(records), payload));

    std::vector<alleycat_device_QuickdrawMatch> matches = decodeTestRequest(payload);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(matches[0].has_player_a_draw_time_ms);
    EXPECT_EQ(matches[0].player_a_draw_time_ms, 0u);
}

inline void matchCodecOmitsUnassignedTeams() {
    std::vector<Match> records = {makeTestMatch(0)};

    std::string payload;
    ASSERT_TRUE(QuickdrawMatchCodec::encodeRequest(records.size(), makeMatchSource(records), payload));

    std::vector<alleycat_device_QuickdrawMatch> matches = decodeTestRequest(payload);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_FALSE(matches[0].has_player_a_team_id);
    EXPECT_FALSE(matches[0].has_player_b_team_id);
}

inline void matchCodecFailsWhenSourceFails() {
    std::string payload;
    QuickdrawMatchCodec::MatchSource failingSource = [](size_t index, FdnStoredMatchEntry& entry) {
        makeTestMatch(index).serialize(entry.content.recordBytes);
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
    EXPECT_EQ(rejection.matchId, makeTestUuid(source.match_id));
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

inline void matchCodecRejectsMalformedMatchIdWithoutDelivering() {
    std::vector<alleycat_device_QuickdrawMatchRejection> sources = {
        makeRejectionMessage(0, "00000000-0000-4000-8000-000000000000", 42),
        makeRejectionMessage(1, "not-a-uuid", 42),
    };

    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    EXPECT_FALSE(QuickdrawMatchCodec::decodeResponse(encodeUploadResponse(sources), collectRejections(rejections), result));
    EXPECT_TRUE(rejections.empty());
}

inline void matchCodecRejectsGarbageBody() {
    std::vector<QuickdrawMatchRejection> rejections;
    QuickdrawUploadResult result;
    EXPECT_FALSE(QuickdrawMatchCodec::decodeResponse("not a protobuf", collectRejections(rejections), result));
}

// ---- Upload API ----

inline void matchesApiRejectsOutOfRangeCounts(AlleycatApiTestSuite* suite) {
    std::vector<Match> records = {makeTestMatch(0)};
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
    std::vector<Match> records = {makeTestMatch(0)};
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
