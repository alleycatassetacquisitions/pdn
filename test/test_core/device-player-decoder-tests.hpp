#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/device-player.hpp"
#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/player-api.hpp"

inline alleycat_device_DevicePlayerResponse makeUnassignedPlayerMessage() {
    alleycat_device_DevicePlayerResponse message = alleycat_device_DevicePlayerResponse_init_zero;
    IdGenerator::copyId(message.id, "7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11");
    IdGenerator::copyId(message.event_id, "0b9e2f4a-5c7d-4e8f-a1b2-c3d4e5f60718");
    IdGenerator::copyId(message.pdn_code, "0007");
    IdGenerator::copyId(message.name, "Alex");
    message.created_at_unix_ms = 1792359000000;
    message.active = true;
    return message;
}

inline alleycat_device_DevicePlayerAffiliation makeAffiliationMessage(
    const char* categoryId, const char* categoryName, const char* affiliationId, const char* affiliationName) {
    alleycat_device_DevicePlayerAffiliation affiliation = alleycat_device_DevicePlayerAffiliation_init_zero;
    IdGenerator::copyId(affiliation.category_id, categoryId);
    IdGenerator::copyId(affiliation.category_name, categoryName);
    IdGenerator::copyId(affiliation.affiliation_id, affiliationId);
    IdGenerator::copyId(affiliation.affiliation_name, affiliationName);
    return affiliation;
}

inline bool encodeTestAffiliations(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const std::vector<DevicePlayerAffiliation>* affiliations =
        static_cast<const std::vector<DevicePlayerAffiliation>*>(*arg);
    for (const DevicePlayerAffiliation& row : *affiliations) {
        alleycat_device_DevicePlayerAffiliation message = alleycat_device_DevicePlayerAffiliation_init_zero;
        IdGenerator::copyId(message.category_id, row.categoryId);
        IdGenerator::copyId(message.category_name, row.categoryName);
        IdGenerator::copyId(message.affiliation_id, row.affiliationId);
        IdGenerator::copyId(message.affiliation_name, row.affiliationName);
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, alleycat_device_DevicePlayerAffiliation_fields, &message)) {
            return false;
        }
    }
    return true;
}

inline std::string encodeDevicePlayerResponse(
    const alleycat_device_DevicePlayerResponse& header, const std::vector<DevicePlayerAffiliation>& affiliations) {
    alleycat_device_DevicePlayerResponse message = header;
    message.affiliations.funcs.encode = &encodeTestAffiliations;
    message.affiliations.arg = const_cast<std::vector<DevicePlayerAffiliation>*>(&affiliations);
    return encodeTestMessage(alleycat_device_DevicePlayerResponse_fields, &message);
}

inline void devicePlayerDecodesTeamAndAffiliations() {
    alleycat_device_DevicePlayerResponse header = makeUnassignedPlayerMessage();
    header.has_team_id = true;
    IdGenerator::copyId(header.team_id, "5a4b3c2d-1e0f-4a9b-8c7d-6e5f4a3b2c1d");
    header.has_team_name = true;
    IdGenerator::copyId(header.team_name, "Night Shift");
    std::vector<DevicePlayerAffiliation> affiliations;
    affiliations.push_back({});
    IdGenerator::copyId(affiliations[0].categoryId, "c1");
    IdGenerator::copyId(affiliations[0].categoryName, "Allegiance");
    IdGenerator::copyId(affiliations[0].affiliationId, "a1");
    IdGenerator::copyId(affiliations[0].affiliationName, "Helix Industries");
    affiliations.push_back({});
    IdGenerator::copyId(affiliations[1].categoryId, "c2");
    IdGenerator::copyId(affiliations[1].categoryName, "Faction");
    IdGenerator::copyId(affiliations[1].affiliationId, "a2");
    IdGenerator::copyId(affiliations[1].affiliationName, "Runners");

    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeDevicePlayerResponse(header, affiliations), player));

    EXPECT_STREQ(player.id, header.id);
    EXPECT_STREQ(player.eventId, header.event_id);
    EXPECT_STREQ(player.pdnCode, "0007");
    EXPECT_STREQ(player.name, "Alex");
    EXPECT_EQ(player.createdAtUnixMs, header.created_at_unix_ms);
    EXPECT_TRUE(player.active);
    EXPECT_TRUE(player.hasTeam());
    EXPECT_STREQ(player.teamId, header.team_id);
    EXPECT_STREQ(player.teamName, "Night Shift");
    ASSERT_EQ(player.affiliations.size(), 2u);
    EXPECT_STREQ(player.affiliations[0].categoryId, "c1");
    EXPECT_STREQ(player.affiliations[0].categoryName, "Allegiance");
    EXPECT_STREQ(player.affiliations[0].affiliationId, "a1");
    EXPECT_STREQ(player.affiliations[0].affiliationName, "Helix Industries");
    EXPECT_STREQ(player.affiliations[1].categoryName, "Faction");
    EXPECT_STREQ(player.affiliations[1].affiliationName, "Runners");
}

inline void devicePlayerDecodesUnassignedPlayer() {
    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(
        encodeDevicePlayerResponse(makeUnassignedPlayerMessage(), {}), player));

    EXPECT_FALSE(player.hasTeam());
    EXPECT_STREQ(player.teamName, "");
    EXPECT_TRUE(player.affiliations.empty());
}

inline void devicePlayerKeepsInactivePlayer() {
    alleycat_device_DevicePlayerResponse header = makeUnassignedPlayerMessage();
    header.active = false;

    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeDevicePlayerResponse(header, {}), player));

    EXPECT_FALSE(player.active);
    EXPECT_STREQ(player.id, header.id);
}

inline void devicePlayerDecodesMaximumLengthName() {
    alleycat_device_DevicePlayerResponse header = makeUnassignedPlayerMessage();
    const std::string name(DevicePlayerLimits::NAME_BUFFER_SIZE - 1, 'x');
    IdGenerator::copyId(header.name, name.c_str());

    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeDevicePlayerResponse(header, {}), player));

    EXPECT_EQ(std::string(player.name), name);
}

inline void devicePlayerRejectsOversizedName() {
    std::string body = encodeDevicePlayerResponse(makeUnassignedPlayerMessage(), {});
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_name_tag,
                         std::string(DevicePlayerLimits::NAME_BUFFER_SIZE, 'x'));

    DevicePlayer player;
    EXPECT_FALSE(DevicePlayerDecoder::decode(body, player));
}

inline void devicePlayerDecodesMaximumAffiliations() {
    std::vector<DevicePlayerAffiliation> affiliations(DevicePlayerLimits::MAX_AFFILIATIONS);
    for (DevicePlayerAffiliation& row : affiliations) {
        IdGenerator::copyId(row.categoryId, "c");
        IdGenerator::copyId(row.categoryName, "Category");
        IdGenerator::copyId(row.affiliationId, "a");
        IdGenerator::copyId(row.affiliationName, "Affiliation");
    }

    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(
        encodeDevicePlayerResponse(makeUnassignedPlayerMessage(), affiliations), player));

    EXPECT_EQ(player.affiliations.size(), DevicePlayerLimits::MAX_AFFILIATIONS);
}

inline void devicePlayerRejectsTooManyAffiliations() {
    std::vector<DevicePlayerAffiliation> affiliations(DevicePlayerLimits::MAX_AFFILIATIONS);
    for (DevicePlayerAffiliation& row : affiliations) {
        IdGenerator::copyId(row.categoryId, "c");
        IdGenerator::copyId(row.categoryName, "Category");
        IdGenerator::copyId(row.affiliationId, "a");
        IdGenerator::copyId(row.affiliationName, "Affiliation");
    }
    const alleycat_device_DevicePlayerAffiliation extra =
        makeAffiliationMessage("c", "Category", "a", "Affiliation");
    std::string body = encodeDevicePlayerResponse(makeUnassignedPlayerMessage(), affiliations);
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_affiliations_tag,
                         encodeTestMessage(alleycat_device_DevicePlayerAffiliation_fields, &extra));

    DevicePlayer player;
    player.affiliations.push_back({});
    EXPECT_FALSE(DevicePlayerDecoder::decode(body, player));
    EXPECT_EQ(player.affiliations.size(), 1u);
}

inline void devicePlayerRejectsGarbageBody() {
    DevicePlayer player;
    EXPECT_FALSE(DevicePlayerDecoder::decode("not a protobuf", player));
}

inline void playerApiQueuesDevicePlayerLookup(AlleycatApiTestSuite* suite) {
    PlayerApi::getDevicePlayerByPin(
        suite->wirelessManager,
        "0007",
        [](const DevicePlayer&) {},
        [](const WirelessErrorInfo&) {});

    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].method, "GET");
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-pin/0007");
    EXPECT_TRUE(suite->queuedRequests[0].payload.empty());
}