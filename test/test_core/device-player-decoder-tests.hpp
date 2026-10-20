#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/device-player.hpp"
#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/player-api.hpp"

inline bool encodeTestAffiliations(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const std::vector<DevicePlayerAffiliation>* affiliations =
        static_cast<const std::vector<DevicePlayerAffiliation>*>(*arg);
    for (const DevicePlayerAffiliation& affiliation : *affiliations) {
        alleycat_device_DevicePlayerAffiliation message = alleycat_device_DevicePlayerAffiliation_init_zero;
        bindTestString(message.category_id, affiliation.categoryId);
        bindTestString(message.category_name, affiliation.categoryName);
        bindTestString(message.affiliation_id, affiliation.affiliationId);
        bindTestString(message.affiliation_name, affiliation.affiliationName);
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, alleycat_device_DevicePlayerAffiliation_fields, &message)) {
            return false;
        }
    }
    return true;
}

inline std::string encodeDevicePlayerResponse(const DevicePlayer& player) {
    alleycat_device_DevicePlayerResponse message = alleycat_device_DevicePlayerResponse_init_zero;
    bindTestString(message.id, player.id);
    bindTestString(message.event_id, player.eventId);
    bindTestString(message.pdn_code, player.pdnCode);
    bindTestString(message.name, player.name);
    message.created_at_unix_ms = player.createdAtUnixMs;
    if (player.hasTeam()) {
        bindTestString(message.team_id, player.teamId);
        bindTestString(message.team_name, player.teamName);
    }
    message.active = player.active;
    message.affiliations.funcs.encode = &encodeTestAffiliations;
    message.affiliations.arg = const_cast<std::vector<DevicePlayerAffiliation>*>(&player.affiliations);
    return encodeTestMessage(alleycat_device_DevicePlayerResponse_fields, &message);
}

inline DevicePlayer makeUnassignedDevicePlayer() {
    DevicePlayer player;
    player.id = "7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11";
    player.eventId = "0b9e2f4a-5c7d-4e8f-a1b2-c3d4e5f60718";
    player.pdnCode = "0007";
    player.name = "Alex";
    player.createdAtUnixMs = 1792359000000;
    player.active = true;
    return player;
}

inline void devicePlayerDecodesTeamAndAffiliations() {
    DevicePlayer source = makeUnassignedDevicePlayer();
    source.teamId = "5a4b3c2d-1e0f-4a9b-8c7d-6e5f4a3b2c1d";
    source.teamName = "Night Shift";
    source.affiliations.push_back({"c1", "Allegiance", "a1", "Helix Industries"});
    source.affiliations.push_back({"c2", "Faction", "a2", "Runners"});

    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeDevicePlayerResponse(source), player));

    EXPECT_EQ(player.id, source.id);
    EXPECT_EQ(player.eventId, source.eventId);
    EXPECT_EQ(player.pdnCode, "0007");
    EXPECT_EQ(player.name, "Alex");
    EXPECT_EQ(player.createdAtUnixMs, source.createdAtUnixMs);
    EXPECT_TRUE(player.active);
    EXPECT_TRUE(player.hasTeam());
    EXPECT_EQ(player.teamId, source.teamId);
    EXPECT_EQ(player.teamName, "Night Shift");
    ASSERT_EQ(player.affiliations.size(), 2u);
    EXPECT_EQ(player.affiliations[0].categoryId, "c1");
    EXPECT_EQ(player.affiliations[0].categoryName, "Allegiance");
    EXPECT_EQ(player.affiliations[0].affiliationId, "a1");
    EXPECT_EQ(player.affiliations[0].affiliationName, "Helix Industries");
    EXPECT_EQ(player.affiliations[1].categoryName, "Faction");
    EXPECT_EQ(player.affiliations[1].affiliationName, "Runners");
}

inline void devicePlayerDecodesUnassignedPlayer() {
    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeDevicePlayerResponse(makeUnassignedDevicePlayer()), player));

    EXPECT_FALSE(player.hasTeam());
    EXPECT_TRUE(player.teamName.empty());
    EXPECT_TRUE(player.affiliations.empty());
}

inline void devicePlayerKeepsInactivePlayer() {
    DevicePlayer source = makeUnassignedDevicePlayer();
    source.active = false;

    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeDevicePlayerResponse(source), player));

    EXPECT_FALSE(player.active);
    EXPECT_EQ(player.id, source.id);
}

inline void devicePlayerDecodesLongName() {
    DevicePlayer source = makeUnassignedDevicePlayer();
    source.name = std::string(200, 'x');

    DevicePlayer player;
    ASSERT_TRUE(DevicePlayerDecoder::decode(encodeDevicePlayerResponse(source), player));

    EXPECT_EQ(player.name, source.name);
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
