#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/player-api.hpp"
#include "game/player-types.hpp"
#include "uuid-bytes-tests.hpp"

constexpr const char* TEST_PLAYER_ID = "7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11";
constexpr const char* TEST_TEAM_ID = "5a4b3c2d-1e0f-4a9b-8c7d-6e5f4a3b2c1d";
constexpr const char* TEST_CATEGORY_ID = "c0ffee00-0000-4000-8000-000000000001";
constexpr const char* TEST_AFFILIATION_ID = "c0ffee00-0000-4000-8000-000000000002";
constexpr size_t TEST_DISPLAY_NAME_LENGTH = PlayerLimits::NAME_BUFFER_SIZE - 1;
constexpr size_t TEST_SERVER_NAME_LENGTH = 255;

/** The 16-byte network-order form the server sends for a UUID. */
inline std::string uuidWireBytes(const char* text) {
    const UUIDBytes id = makeTestUuid(text);
    return std::string(reinterpret_cast<const char*>(id.bytes), UUIDBytes::SIZE);
}

// Fixtures are written field by field so they can carry values the device truncates or rejects.

/** Encodes a player header; an empty name is omitted, as proto3 omits it. */
inline std::string encodeTestPlayer(const std::string& id, const std::string& name) {
    std::string body;
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_id_tag, id);
    if (!name.empty()) {
        appendTestBytesField(body, alleycat_device_DevicePlayerResponse_name_tag, name);
    }
    return body;
}

inline void appendTestTeam(std::string& body, const std::string& teamId, const std::string& teamName) {
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_team_id_tag, teamId);
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_team_name_tag, teamName);
}

inline void appendTestAffiliation(std::string& body,
                                  const std::string& categoryId,
                                  const std::string& categoryName,
                                  const std::string& affiliationId,
                                  const std::string& affiliationName) {
    std::string affiliation;
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_category_id_tag, categoryId);
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_category_name_tag, categoryName);
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_id_tag, affiliationId);
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_name_tag, affiliationName);
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_affiliations_tag, affiliation);
}

inline void appendTestAffiliationNamed(std::string& body, const std::string& affiliationName) {
    appendTestAffiliation(body,
                          uuidWireBytes(TEST_CATEGORY_ID),
                          "Category",
                          uuidWireBytes(TEST_AFFILIATION_ID),
                          affiliationName);
}

inline void devicePlayerDecodesTeamAndAffiliations() {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Night Shift");
    appendTestAffiliation(body,
                          uuidWireBytes("c1c1c1c1-0000-4000-8000-000000000001"),
                          "Allegiance",
                          uuidWireBytes("a1a1a1a1-0000-4000-8000-000000000001"),
                          "Helix Industries");
    appendTestAffiliation(body,
                          uuidWireBytes("c2c2c2c2-0000-4000-8000-000000000002"),
                          "Faction",
                          uuidWireBytes("a2a2a2a2-0000-4000-8000-000000000002"),
                          "Runners");

    PlayerV2 player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(body, player));

    EXPECT_EQ(player.id, makeTestUuid(TEST_PLAYER_ID));
    EXPECT_STREQ(player.name, "Alex");
    EXPECT_TRUE(player.hasTeam());
    EXPECT_EQ(player.team.id, makeTestUuid(TEST_TEAM_ID));
    EXPECT_STREQ(player.team.name, "Night Shift");
    ASSERT_EQ(player.affiliations.size(), 2u);
    EXPECT_EQ(player.affiliations[0].categoryId, makeTestUuid("c1c1c1c1-0000-4000-8000-000000000001"));
    EXPECT_STREQ(player.affiliations[0].categoryName, "Allegiance");
    EXPECT_EQ(player.affiliations[0].affiliationId, makeTestUuid("a1a1a1a1-0000-4000-8000-000000000001"));
    EXPECT_STREQ(player.affiliations[0].affiliationName, "Helix Industries");
    EXPECT_EQ(player.affiliations[1].categoryId, makeTestUuid("c2c2c2c2-0000-4000-8000-000000000002"));
    EXPECT_STREQ(player.affiliations[1].categoryName, "Faction");
    EXPECT_EQ(player.affiliations[1].affiliationId, makeTestUuid("a2a2a2a2-0000-4000-8000-000000000002"));
    EXPECT_STREQ(player.affiliations[1].affiliationName, "Runners");
}

inline void devicePlayerDecodesUnassignedPlayer() {
    PlayerV2 player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex"), player));

    EXPECT_FALSE(player.hasTeam());
    EXPECT_STREQ(player.team.name, "");
    EXPECT_TRUE(player.affiliations.empty());
}

inline void devicePlayerDecodesMaximumLengthName() {
    const std::string name(TEST_DISPLAY_NAME_LENGTH, 'x');

    PlayerV2 player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), name), player));

    EXPECT_EQ(std::string(player.name), name);
}

inline void devicePlayerTruncatesLongDisplayNames() {
    const std::string longName(TEST_SERVER_NAME_LENGTH, 'n');
    const std::string displayName(TEST_DISPLAY_NAME_LENGTH, 'n');
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), longName);
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), longName);
    appendTestAffiliation(body,
                          uuidWireBytes(TEST_CATEGORY_ID),
                          longName,
                          uuidWireBytes(TEST_AFFILIATION_ID),
                          longName);

    PlayerV2 player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(body, player));

    EXPECT_EQ(std::string(player.name), displayName);
    EXPECT_EQ(std::string(player.team.name), displayName);
    ASSERT_EQ(player.affiliations.size(), 1u);
    EXPECT_EQ(std::string(player.affiliations[0].categoryName), displayName);
    EXPECT_EQ(std::string(player.affiliations[0].affiliationName), displayName);
    EXPECT_EQ(player.affiliations[0].affiliationId, makeTestUuid(TEST_AFFILIATION_ID))
        << "fields after a truncated name must still decode";
}

inline void devicePlayerDecodesAbsentName() {
    PlayerV2 player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), ""), player));

    EXPECT_STREQ(player.name, "");
}

inline void devicePlayerDecodesMaximumAffiliations() {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    for (size_t index = 0; index < PlayerLimits::MAX_AFFILIATIONS; index++) {
        appendTestAffiliationNamed(body, "Affiliation");
    }

    PlayerV2 player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(body, player));

    EXPECT_EQ(player.affiliations.size(), PlayerLimits::MAX_AFFILIATIONS);
}

inline void devicePlayerDropsAffiliationsPastLimit() {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestAffiliationNamed(body, "First");
    appendTestAffiliationNamed(body, "Second");
    appendTestAffiliationNamed(body, "Third");
    appendTestAffiliationNamed(body, "Fourth");
    appendTestAffiliationNamed(body, "Fifth");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Night Shift");

    PlayerV2 player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(body, player));

    ASSERT_EQ(player.affiliations.size(), PlayerLimits::MAX_AFFILIATIONS);
    EXPECT_STREQ(player.affiliations[0].affiliationName, "First");
    EXPECT_STREQ(player.affiliations[1].affiliationName, "Second");
    EXPECT_STREQ(player.affiliations[2].affiliationName, "Third");
    EXPECT_STREQ(player.team.name, "Night Shift")
    << "fields after skipped affiliations must still decode";
}

inline void devicePlayerRejectsWrongLengthUuids() {
    const std::string shortId = uuidWireBytes(TEST_PLAYER_ID).substr(0, UUIDBytes::SIZE - 1);
    const std::string longId = uuidWireBytes(TEST_TEAM_ID) + '\0';
    const std::string textId = TEST_AFFILIATION_ID;

    std::string badTeamId = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestTeam(badTeamId, longId, "Night Shift");

    std::string badAffiliationId = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestAffiliation(badAffiliationId, uuidWireBytes(TEST_CATEGORY_ID), "Category", textId, "Affiliation");

    PlayerV2 player;
    player.affiliations.push_back({});
    EXPECT_FALSE(DevicePlayerDecoder::decode(encodeTestPlayer(shortId, "Alex"), player));
    EXPECT_FALSE(DevicePlayerDecoder::decode(badTeamId, player));
    EXPECT_FALSE(DevicePlayerDecoder::decode(badAffiliationId, player));
    EXPECT_TRUE(player.id.isNil()) << "a failed decode must leave the player untouched";
    EXPECT_EQ(player.affiliations.size(), 1u);
}

inline void devicePlayerRejectsMissingRequiredUuids() {
    std::string missingPlayerId;
    appendTestBytesField(missingPlayerId, alleycat_device_DevicePlayerResponse_name_tag, "Alex");

    std::string missingCategoryId = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    std::string affiliation;
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_category_name_tag, "Category");
    appendTestBytesField(
        affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_id_tag, uuidWireBytes(TEST_AFFILIATION_ID));
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_name_tag, "Affiliation");
    appendTestBytesField(missingCategoryId, alleycat_device_DevicePlayerResponse_affiliations_tag, affiliation);

    PlayerV2 player;
    EXPECT_FALSE(DevicePlayerDecoder::decode(missingPlayerId, player));
    EXPECT_FALSE(DevicePlayerDecoder::decode(missingCategoryId, player));
}

inline void devicePlayerRejectsGarbageBody() {
    PlayerV2 player;
    EXPECT_FALSE(DevicePlayerDecoder::decode("not a protobuf", player));
}

inline void playerApiQueuesDevicePlayerLookup(AlleycatApiTestSuite* suite) {
    PlayerApi::getDevicePlayerByPin(
    suite->wirelessManager, "0007", [](const PlayerV2&) {}, [](const WirelessErrorInfo&) {});

    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].method, "GET");
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-pin/0007");
    EXPECT_TRUE(suite->queuedRequests[0].payload.empty());
}
