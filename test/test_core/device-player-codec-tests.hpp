#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/device-player-codec.hpp"
#include "alleycat-server/player-api.hpp"
#include "game/player.hpp"
#include "game/player-types.hpp"
#include "game/record-key.hpp"
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

/** A record key as the server sends it: @p length bytes counting up from 0xC0. */
inline std::string testRecordKeyWire(size_t length = RECORD_KEY_SIZE) {
    std::string key;
    for (size_t i = 0; i < length; ++i) {
        key.push_back(static_cast<char>(0xC0 + i));
    }
    return key;
}

/** True when @p recordKey holds exactly the bytes of testRecordKeyWire(). */
inline bool holdsTestRecordKey(const RecordKey& recordKey) {
    return recordKey.isHeld() &&
           memcmp(recordKey.getBytes(), testRecordKeyWire().data(), RECORD_KEY_SIZE) == 0;
}

// SERVER-SEAM(pdn-record-auth-and-relay): record_key is a provisional field.
inline void appendTestRecordKey(std::string& body, const std::string& key) {
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_record_key_tag, key);
}

/** Encodes a player header with no record key; an empty name is omitted, as proto3 omits it. */
inline std::string encodeTestPlayerWithoutKey(const std::string& id, const std::string& name) {
    std::string body;
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_id_tag, id);
    if (!name.empty()) {
        appendTestBytesField(body, alleycat_device_DevicePlayerResponse_name_tag, name);
    }
    return body;
}

/** Encodes a player header carrying testRecordKeyWire(), as a server that issues keys answers. */
inline std::string encodeTestPlayer(const std::string& id, const std::string& name) {
    std::string body = encodeTestPlayerWithoutKey(id, name);
    appendTestRecordKey(body, testRecordKeyWire());
    return body;
}

/** Decodes the profile of @p body for a test that does not look at the record key. */
inline bool decodeTestProfile(const std::string& body, Player& player) {
    RecordKey recordKey;
    return DevicePlayerCodec::decode(body, player, recordKey);
}

inline void appendTestTeam(std::string& body, const std::string& teamId,
                           const std::string& teamName) {
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_team_id_tag, teamId);
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_team_name_tag, teamName);
}

inline void appendTestAffiliation(std::string& body, const std::string& categoryId,
                                  const std::string& categoryName, const std::string& affiliationId,
                                  const std::string& affiliationName) {
    std::string affiliation;
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_category_id_tag,
                         categoryId);
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_category_name_tag,
                         categoryName);
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_id_tag,
                         affiliationId);
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_name_tag,
                         affiliationName);
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_affiliations_tag, affiliation);
}

inline void appendTestAffiliationNamed(std::string& body, const std::string& affiliationName) {
    appendTestAffiliation(body, uuidWireBytes(TEST_CATEGORY_ID), "Category",
                          uuidWireBytes(TEST_AFFILIATION_ID), affiliationName);
}

inline void devicePlayerDecodesTeamAndAffiliations() {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Night Shift");
    appendTestAffiliation(body, uuidWireBytes("c1c1c1c1-0000-4000-8000-000000000001"), "Allegiance",
                          uuidWireBytes("a1a1a1a1-0000-4000-8000-000000000001"),
                          "Helix Industries");
    appendTestAffiliation(body, uuidWireBytes("c2c2c2c2-0000-4000-8000-000000000002"), "Faction",
                          uuidWireBytes("a2a2a2a2-0000-4000-8000-000000000002"), "Runners");

    Player player;
    ASSERT_TRUE(decodeTestProfile(body, player));

    EXPECT_EQ(player.getServerPlayerId(), makeTestUuid(TEST_PLAYER_ID));
    EXPECT_STREQ(player.getServerPlayerName(), "Alex");
    EXPECT_TRUE(player.hasTeam());
    EXPECT_EQ(player.getRegistrationTeam().id, makeTestUuid(TEST_TEAM_ID));
    EXPECT_STREQ(player.getRegistrationTeam().name, "Night Shift");
    ASSERT_EQ(player.getRegistrationAffiliations().size(), 2u);
    EXPECT_EQ(player.getRegistrationAffiliations()[0].categoryId,
              makeTestUuid("c1c1c1c1-0000-4000-8000-000000000001"));
    EXPECT_STREQ(player.getRegistrationAffiliations()[0].categoryName, "Allegiance");
    EXPECT_EQ(player.getRegistrationAffiliations()[0].affiliationId,
              makeTestUuid("a1a1a1a1-0000-4000-8000-000000000001"));
    EXPECT_STREQ(player.getRegistrationAffiliations()[0].affiliationName, "Helix Industries");
    EXPECT_EQ(player.getRegistrationAffiliations()[1].categoryId,
              makeTestUuid("c2c2c2c2-0000-4000-8000-000000000002"));
    EXPECT_STREQ(player.getRegistrationAffiliations()[1].categoryName, "Faction");
    EXPECT_EQ(player.getRegistrationAffiliations()[1].affiliationId,
              makeTestUuid("a2a2a2a2-0000-4000-8000-000000000002"));
    EXPECT_STREQ(player.getRegistrationAffiliations()[1].affiliationName, "Runners");
}

inline void devicePlayerDecodesUnassignedPlayer() {
    Player player;
    ASSERT_TRUE(decodeTestProfile(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex"), player));

    EXPECT_FALSE(player.hasTeam());
    EXPECT_STREQ(player.getRegistrationTeam().name, "");
    EXPECT_TRUE(player.getRegistrationAffiliations().empty());
}

inline void devicePlayerDecodesMaximumLengthName() {
    const std::string name(TEST_DISPLAY_NAME_LENGTH, 'x');

    Player player;
    ASSERT_TRUE(decodeTestProfile(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), name), player));

    EXPECT_EQ(std::string(player.getServerPlayerName()), name);
}

inline void devicePlayerTruncatesLongDisplayNames() {
    const std::string longName(TEST_SERVER_NAME_LENGTH, 'n');
    const std::string displayName(TEST_DISPLAY_NAME_LENGTH, 'n');
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), longName);
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), longName);
    appendTestAffiliation(body, uuidWireBytes(TEST_CATEGORY_ID), longName,
                          uuidWireBytes(TEST_AFFILIATION_ID), longName);

    Player player;
    ASSERT_TRUE(decodeTestProfile(body, player));

    EXPECT_EQ(std::string(player.getServerPlayerName()), displayName);
    EXPECT_EQ(std::string(player.getRegistrationTeam().name), displayName);
    ASSERT_EQ(player.getRegistrationAffiliations().size(), 1u);
    EXPECT_EQ(std::string(player.getRegistrationAffiliations()[0].categoryName), displayName);
    EXPECT_EQ(std::string(player.getRegistrationAffiliations()[0].affiliationName), displayName);
    EXPECT_EQ(player.getRegistrationAffiliations()[0].affiliationId,
              makeTestUuid(TEST_AFFILIATION_ID))
    << "fields after a truncated name must still decode";
}

inline void devicePlayerDecodesAbsentName() {
    Player player;
    ASSERT_TRUE(decodeTestProfile(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), ""), player));

    EXPECT_STREQ(player.getServerPlayerName(), "");
}

inline void devicePlayerDecodesMaximumAffiliations() {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    for (size_t index = 0; index < PlayerLimits::MAX_AFFILIATIONS; index++) {
        appendTestAffiliationNamed(body, "Affiliation");
    }

    Player player;
    ASSERT_TRUE(decodeTestProfile(body, player));

    EXPECT_EQ(player.getRegistrationAffiliations().size(), PlayerLimits::MAX_AFFILIATIONS);
}

inline void devicePlayerDropsAffiliationsPastLimit() {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestAffiliationNamed(body, "First");
    appendTestAffiliationNamed(body, "Second");
    appendTestAffiliationNamed(body, "Third");
    appendTestAffiliationNamed(body, "Fourth");
    appendTestAffiliationNamed(body, "Fifth");
    appendTestAffiliationNamed(body, "Sixth");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Night Shift");

    Player player;
    ASSERT_TRUE(decodeTestProfile(body, player));

    ASSERT_EQ(player.getRegistrationAffiliations().size(), PlayerLimits::MAX_AFFILIATIONS);
    EXPECT_STREQ(player.getRegistrationAffiliations()[0].affiliationName, "First");
    EXPECT_STREQ(player.getRegistrationAffiliations()[1].affiliationName, "Second");
    EXPECT_STREQ(player.getRegistrationAffiliations()[2].affiliationName, "Third");
    EXPECT_STREQ(player.getRegistrationAffiliations()[3].affiliationName, "Fourth");
    EXPECT_STREQ(player.getRegistrationAffiliations()[4].affiliationName, "Fifth");
    EXPECT_STREQ(player.getRegistrationTeam().name, "Night Shift")
    << "fields after skipped affiliations must still decode";
}

inline void devicePlayerRejectsWrongLengthUuids() {
    const std::string shortId = uuidWireBytes(TEST_PLAYER_ID).substr(0, UUIDBytes::SIZE - 1);
    const std::string longId = uuidWireBytes(TEST_TEAM_ID) + '\0';
    const std::string textId = TEST_AFFILIATION_ID;

    std::string badTeamId = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestTeam(badTeamId, longId, "Night Shift");

    std::string badAffiliationId = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestAffiliation(badAffiliationId, uuidWireBytes(TEST_CATEGORY_ID), "Category", textId,
                          "Affiliation");

    Player player;
    ASSERT_TRUE(decodeTestProfile(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex"), player));
    const UUIDBytes idBefore = player.getServerPlayerId();
    const size_t affiliationCountBefore = player.getRegistrationAffiliations().size();
    EXPECT_FALSE(decodeTestProfile(encodeTestPlayer(shortId, "Alex"), player));
    EXPECT_FALSE(decodeTestProfile(badTeamId, player));
    EXPECT_FALSE(decodeTestProfile(badAffiliationId, player));
    EXPECT_EQ(player.getServerPlayerId(), idBefore)
    << "a failed decode must leave the player untouched";
    EXPECT_EQ(player.getRegistrationAffiliations().size(), affiliationCountBefore);
}

inline void devicePlayerRejectsMissingRequiredUuids() {
    std::string missingPlayerId;
    appendTestBytesField(missingPlayerId, alleycat_device_DevicePlayerResponse_name_tag, "Alex");

    std::string missingCategoryId = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    std::string affiliation;
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_category_name_tag,
                         "Category");
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_id_tag,
                         uuidWireBytes(TEST_AFFILIATION_ID));
    appendTestBytesField(affiliation, alleycat_device_DevicePlayerAffiliation_affiliation_name_tag,
                         "Affiliation");
    appendTestBytesField(missingCategoryId, alleycat_device_DevicePlayerResponse_affiliations_tag,
                         affiliation);

    Player player;
    EXPECT_FALSE(decodeTestProfile(missingPlayerId, player));
    EXPECT_FALSE(decodeTestProfile(missingCategoryId, player));
}

inline void devicePlayerRejectsGarbageBody() {
    Player player;
    EXPECT_FALSE(decodeTestProfile("not a protobuf", player));
}

inline void playerApiQueuesDevicePlayerLookup(AlleycatApiTestSuite* suite) {
    Player player;
    RecordKey recordKey;
    PlayerApi::getDevicePlayerByPin(
    suite->wirelessManager, "0007", &player, &recordKey, [](Player&) {},
    [](const WirelessErrorInfo&) {});

    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].method, "GET");
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-pin/0007");
    EXPECT_TRUE(suite->queuedRequests[0].payload.empty());
    ASSERT_EQ(suite->queuedRequests[0].requestHeaders.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].requestHeaders[0].first, "Accept");
    EXPECT_EQ(suite->queuedRequests[0].requestHeaders[0].second, "application/protobuf");
}

inline void devicePlayerDecodesRecordKeyBesideProfile() {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Night Shift");

    Player player;
    RecordKey recordKey;
    ASSERT_TRUE(DevicePlayerCodec::decode(body, player, recordKey));

    EXPECT_TRUE(holdsTestRecordKey(recordKey));
    EXPECT_STREQ(player.getServerPlayerName(), "Alex");
    EXPECT_STREQ(player.getRegistrationTeam().name, "Night Shift")
    << "fields after the record key must still decode";
}

// A profile with no key the FDN can use is still a profile: the FDN tells
// "no key issued" apart from "no such player".
inline void devicePlayerDecodesProfileWithoutUsableRecordKey() {
    const std::string id = uuidWireBytes(TEST_PLAYER_ID);
    std::vector<std::string> bodies = {encodeTestPlayerWithoutKey(id, "Alex")};
    for (size_t length : {size_t{0}, RECORD_KEY_SIZE - 1, RECORD_KEY_SIZE + 1, size_t{300}}) {
        bodies.push_back(encodeTestPlayerWithoutKey(id, "Alex"));
        appendTestRecordKey(bodies.back(), testRecordKeyWire(length));
    }
    // The field's last occurrence is the one that counts.
    bodies.push_back(encodeTestPlayer(id, "Alex"));
    appendTestRecordKey(bodies.back(), testRecordKeyWire(RECORD_KEY_SIZE - 1));

    for (const std::string& body : bodies) {
        Player player;
        RecordKey recordKey;
        const uint8_t staleKey[RECORD_KEY_SIZE] = {0x5A};
        recordKey.set(staleKey);
        ASSERT_TRUE(DevicePlayerCodec::decode(body, player, recordKey)) << body.size();
        EXPECT_STREQ(player.getServerPlayerName(), "Alex");
        EXPECT_FALSE(recordKey.isHeld())
        << "a response of " << body.size() << " bytes left a key in the holder";
    }
}

inline void devicePlayerRejectedResponseYieldsNoRecordKey() {
    const std::string nilId(UUIDBytes::SIZE, '\0');
    // The key is read in before the field that makes the response undecodable.
    std::string brokenAfterKey = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestTeam(brokenAfterKey, uuidWireBytes(TEST_TEAM_ID) + '\0', "Night Shift");

    Player player;
    RecordKey recordKey;
    EXPECT_FALSE(DevicePlayerCodec::decode(encodeTestPlayer(nilId, "Alex"), player, recordKey));
    EXPECT_FALSE(recordKey.isHeld()) << "a key was taken from a response with no valid player";
    EXPECT_FALSE(DevicePlayerCodec::decode(brokenAfterKey, player, recordKey));
    EXPECT_FALSE(recordKey.isHeld()) << "a key was taken from a response that did not decode";
}

// A lookup from a server that did not present the pinned key gives a profile
// and no key, however well-formed the key in it is.
inline void playerApiTakesRecordKeyOnlyFromVerifiedResponse(AlleycatApiTestSuite* suite) {
    Player player;
    RecordKey recordKey;
    int answered = 0;
    const std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    const std::function<void(Player&)> onSuccess = [&answered](Player&) { answered++; };
    const std::function<void(const WirelessErrorInfo&)> onError = [](const WirelessErrorInfo&) {
        FAIL() << "a decodable profile was reported as an error";
    };

    ON_CALL(suite->httpClient, isResponseServerVerified()).WillByDefault(testing::Return(false));
    PlayerApi::getDevicePlayerByPin(suite->wirelessManager, "0007", &player, &recordKey, onSuccess,
                                    onError);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onSuccess(body);
    EXPECT_EQ(answered, 1);
    EXPECT_STREQ(player.getServerPlayerName(), "Alex");
    EXPECT_FALSE(recordKey.isHeld()) << "a key from an unverified server was accepted";

    ON_CALL(suite->httpClient, isResponseServerVerified()).WillByDefault(testing::Return(true));
    PlayerApi::getDevicePlayerByPin(suite->wirelessManager, "0007", &player, &recordKey, onSuccess,
                                    onError);
    ASSERT_EQ(suite->queuedRequests.size(), 2u);
    suite->queuedRequests[1].onSuccess(body);
    EXPECT_EQ(answered, 2);
    EXPECT_TRUE(holdsTestRecordKey(recordKey));
}
