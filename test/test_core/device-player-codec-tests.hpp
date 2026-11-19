#pragma once

#include <gtest/gtest.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/device-player-codec.hpp"
#include "alleycat-server/player-api.hpp"
#include "device/driver-names.hpp"
#include "device/drivers/band-reader.hpp"
#include "device/drivers/driver-manager.hpp"
#include "device/drivers/logger.hpp"
#include "fake-band-reader.hpp"
#include "game/player.hpp"
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

/** Decodes @p body as a lookup answer, ignoring any record key. */
inline bool decodeTestProfile(const std::string& body, Player& player) {
    IssuedRecordKey recordKey;
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
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Night Shift");

    Player player;
    ASSERT_TRUE(decodeTestProfile(body, player));

    ASSERT_EQ(player.getRegistrationAffiliations().size(), PlayerLimits::MAX_AFFILIATIONS);
    EXPECT_STREQ(player.getRegistrationAffiliations()[0].affiliationName, "First");
    EXPECT_STREQ(player.getRegistrationAffiliations()[1].affiliationName, "Second");
    EXPECT_STREQ(player.getRegistrationAffiliations()[2].affiliationName, "Third");
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

/** A band UID of the bytes in @p bytes. */
inline BandUid makeTestBandUid(std::initializer_list<uint8_t> bytes) {
    BandUid uid;
    for (uint8_t byte : bytes) {
        uid.bytes[uid.length++] = byte;
    }
    return uid;
}

/** A profile body carrying a record key of @p keyLength bytes, each 0xA5. */
inline std::string encodeTestPlayerWithKey(size_t keyLength) {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex");
    appendTestBytesField(body, alleycat_device_DevicePlayerResponse_record_key_tag,
                         std::string(keyLength, '\xA5'));
    return body;
}

/** Runs by-band lookups and keeps what each reported; player and key are from the last. */
struct TestBandLookup {
    Player player;
    IssuedRecordKey recordKey;
    std::vector<ProvisionLookupResult> results;

    /** Starts a lookup for @p uid through the suite's manager. */
    void start(AlleycatApiTestSuite* suite, const BandUid& uid) {
        startOn(suite->wirelessManager, uid);
    }

    /** Starts a lookup for @p uid through @p wirelessManager. */
    void startOn(WirelessManager* wirelessManager, const BandUid& uid) {
        PlayerApi::getDevicePlayerByBand(
        wirelessManager, uid,
        [this](ProvisionLookupResult result, const Player& found, const IssuedRecordKey& key) {
            results.push_back(result);
            player = found;
            recordKey = key;
        });
    }
};

inline void playerApiQueuesBandLookupAsUpperHex(AlleycatApiTestSuite* suite) {
    TestBandLookup lookup;
    lookup.start(suite, makeTestBandUid({0x04, 0xa2, 0x1b, 0x9c}));
    lookup.start(suite, makeTestBandUid({0x04, 0x5e, 0x21, 0x7a, 0x3b, 0x6c, 0x80}));
    lookup.start(suite,
                 makeTestBandUid({0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xff}));

    ASSERT_EQ(suite->queuedRequests.size(), 3u);
    EXPECT_EQ(suite->queuedRequests[0].method, "GET");
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-band/04A21B9C");
    EXPECT_TRUE(suite->queuedRequests[0].payload.empty());
    ASSERT_EQ(suite->queuedRequests[0].requestHeaders.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].requestHeaders[0].first, "Accept");
    EXPECT_EQ(suite->queuedRequests[0].requestHeaders[0].second, "application/protobuf");
    EXPECT_EQ(suite->queuedRequests[1].path, "/device/players/by-band/045E217A3B6C80");
    EXPECT_EQ(suite->queuedRequests[2].path, "/device/players/by-band/000123456789ABCDEFFF");
    EXPECT_TRUE(lookup.results.empty());
}

// The UID becomes a path segment, so a length anticollision never returns is not sent.
inline void playerApiQueuesNoLookupForUidOfAnotherLength(AlleycatApiTestSuite* suite) {
    TestBandLookup lookup;
    lookup.start(suite, BandUid{});
    lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B}));
    lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C, 0x01}));
    lookup.start(suite, makeTestBandUid({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}));

    EXPECT_TRUE(suite->queuedRequests.empty());
    EXPECT_EQ(lookup.results,
              std::vector<ProvisionLookupResult>(4, ProvisionLookupResult::LOOKUP_FAILED))
    << "the caller was left waiting for a lookup that never went out";
}

inline void playerApiReportsKeyIssuedOnlyForThirtyTwoByteKey(AlleycatApiTestSuite* suite) {
    TestBandLookup lookup;
    const BandUid uid = makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C});
    for (int i = 0; i < 4; i++) {
        lookup.start(suite, uid);
    }
    ASSERT_EQ(suite->queuedRequests.size(), 4u);

    suite->queuedRequests[0].onSuccess(encodeTestPlayerWithKey(ISSUED_RECORD_KEY_SIZE));
    ASSERT_EQ(lookup.results.size(), 1u);
    EXPECT_EQ(lookup.results[0], ProvisionLookupResult::KEY_ISSUED);
    EXPECT_TRUE(lookup.recordKey.held);
    for (uint8_t byte : lookup.recordKey.bytes) {
        ASSERT_EQ(byte, 0xA5);
    }
    EXPECT_EQ(lookup.player.getServerPlayerId(), makeTestUuid(TEST_PLAYER_ID));

    suite->queuedRequests[1].onSuccess(encodeTestPlayerWithKey(ISSUED_RECORD_KEY_SIZE - 1));
    suite->queuedRequests[2].onSuccess(encodeTestPlayerWithKey(ISSUED_RECORD_KEY_SIZE + 1));
    suite->queuedRequests[3].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Alex"));
    EXPECT_EQ(lookup.results,
              (std::vector<ProvisionLookupResult>{
              ProvisionLookupResult::KEY_ISSUED, ProvisionLookupResult::KEY_NOT_ISSUED,
              ProvisionLookupResult::KEY_NOT_ISSUED, ProvisionLookupResult::KEY_NOT_ISSUED}));
    EXPECT_FALSE(lookup.recordKey.held) << "a key of the wrong length, or none, was kept";
}

// A failed decode must not leave a key behind from the same answer.
inline void playerApiReportsUndecodableAnswerAsFailedWithoutKey(AlleycatApiTestSuite* suite) {
    TestBandLookup lookup;
    lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C}));
    ASSERT_EQ(suite->queuedRequests.size(), 1u);

    std::string nilPlayer = encodeTestPlayer(std::string(UUIDBytes::SIZE, '\0'), "Alex");
    appendTestBytesField(nilPlayer, alleycat_device_DevicePlayerResponse_record_key_tag,
                         std::string(ISSUED_RECORD_KEY_SIZE, '\xA5'));
    suite->queuedRequests[0].onSuccess(nilPlayer);

    ASSERT_EQ(lookup.results.size(), 1u);
    EXPECT_EQ(lookup.results[0], ProvisionLookupResult::LOOKUP_FAILED);
    EXPECT_FALSE(lookup.recordKey.held);
    EXPECT_TRUE(lookup.player.getServerPlayerId().isZero())
    << "an undecodable profile was handed on";
}

// A found answer followed by a 404 must not hand the earlier player's key on again.
inline void playerApiHandsNoKeyWithFailedLookup(AlleycatApiTestSuite* suite) {
    TestBandLookup lookup;
    lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C}));
    lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9D}));
    ASSERT_EQ(suite->queuedRequests.size(), 2u);
    suite->queuedRequests[0].onSuccess(encodeTestPlayerWithKey(ISSUED_RECORD_KEY_SIZE));
    ASSERT_TRUE(lookup.recordKey.held);

    suite->queuedRequests[1].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    EXPECT_EQ(lookup.results.back(), ProvisionLookupResult::NO_PLAYER);
    EXPECT_FALSE(lookup.recordKey.held);
    EXPECT_TRUE(lookup.player.getServerPlayerId().isZero());
}

inline void playerApiClassifiesLookupErrors(AlleycatApiTestSuite* suite) {
    const std::vector<std::pair<WirelessErrorInfo, ProvisionLookupResult>> cases = {
    {{WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404}, ProvisionLookupResult::NO_PLAYER},
    {{WirelessError::INVALID_RESPONSE, "HTTP Error: 400", 400},
     ProvisionLookupResult::LOOKUP_FAILED},
    {{WirelessError::SERVER_ERROR, "HTTP Error: 500", 500}, ProvisionLookupResult::LOOKUP_FAILED},
    {{WirelessError::TIMEOUT, "Request timed out"}, ProvisionLookupResult::LOOKUP_FAILED},
    {{WirelessError::SERVER_NOT_TRUSTED, "Server certificate not verified"},
     ProvisionLookupResult::SERVER_NOT_TRUSTED},
    };
    TestBandLookup lookup;
    for (size_t i = 0; i < cases.size(); i++) {
        lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C}));
        ASSERT_EQ(suite->queuedRequests.size(), i + 1);
        suite->queuedRequests[i].onError(cases[i].first);
        ASSERT_EQ(lookup.results.size(), i + 1);
        EXPECT_EQ(lookup.results[i], cases[i].second) << cases[i].first.message;
    }
}

// With a server address that is not https:// the client refuses at queue time;
// the lookup still ends exactly once.
inline void playerApiReportsRefusedLookupAsUntrustedOnce(AlleycatApiTestSuite* suite) {
    ON_CALL(suite->httpClient, queueRequest(testing::_))
    .WillByDefault(testing::Invoke([](HttpRequest&& request) {
        request.onError({WirelessError::SERVER_NOT_TRUSTED, "Server address is not https://"});
        return false;
    }));
    TestBandLookup lookup;
    lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C}));

    EXPECT_EQ(lookup.results,
              std::vector<ProvisionLookupResult>{ProvisionLookupResult::SERVER_NOT_TRUSTED});
}

// A device with no HTTP client (the CLI FDN) still ends the lookup, as failed.
inline void playerApiEndsLookupWithoutHttpClient(AlleycatApiTestSuite* suite) {
    WirelessManager noClient(&suite->peerComms);
    TestBandLookup lookup;
    lookup.startOn(&noClient, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C}));

    EXPECT_EQ(lookup.results,
              std::vector<ProvisionLookupResult>{ProvisionLookupResult::LOOKUP_FAILED});
}

/** Keeps every formatted log line, for checking what a log would show. */
class CapturingLogger : public LoggerInterface {
public:
    /** Keeps the formatted line, prefixed with @p tag. */
    void vlog(LogLevel level, const char* tag, const char* file, int line, const char* format,
              va_list args) override {
        (void)level;
        (void)file;
        (void)line;
        char text[512];
        vsnprintf(text, sizeof(text), format, args);
        lines.push_back(std::string(tag) + " " + text);
    }

    std::vector<std::string> lines;
};

// #456: a band UID is never logged, by the lookup or by a driver logging its request.
inline void playerApiNeverLogsBandUid(AlleycatApiTestSuite* suite) {
    CapturingLogger logger;
    g_logger = &logger;
    TestBandLookup lookup;
    const BandUid uid = makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C, 0x3E, 0x5F, 0x80});
    for (int i = 0; i < 3; i++) {
        lookup.start(suite, uid);
    }
    ASSERT_EQ(suite->queuedRequests.size(), 3u);
    suite->queuedRequests[0].onSuccess("not a protobuf");
    suite->queuedRequests[1].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    suite->queuedRequests[2].onError({WirelessError::TIMEOUT, "Request timed out"});
    lookup.start(suite, makeTestBandUid({0x04, 0xA2, 0x1B, 0x9C, 0x3E}));
    g_logger = nullptr;

    for (const std::string& line : logger.lines) {
        EXPECT_EQ(line.find("04A21B9C"), std::string::npos) << line;
        EXPECT_EQ(line.find("04a21b9c"), std::string::npos) << line;
    }
    const HttpRequest& request = suite->queuedRequests[0];
    EXPECT_EQ(request.getLoggablePath(), "/device/players/by-band/");
    EXPECT_TRUE(request.hasPrivatePath());
}

// Requests other than the band lookup log their whole path, error text included.
inline void httpRequestWithoutPrivatePartLogsWholePath() {
    HttpRequest request("/quickdraw-matches", "POST", "application/protobuf", "", nullptr, nullptr);
    EXPECT_EQ(request.getLoggablePath(), "/quickdraw-matches");
    EXPECT_FALSE(request.hasPrivatePath());
}

// A reader registered under the band reader name is found through the driver
// framework's cross-cast and reports taps to its handler; an FDN that registers
// none finds none.
inline void bandReaderIsReachedThroughDriverFramework() {
    FakeBandReader* reader = new FakeBandReader();
    DriverManager withReader({{BAND_READER_DRIVER_NAME, reader}});
    DriverInterface* driver = withReader.getDriver(BAND_READER_DRIVER_NAME);
    ASSERT_NE(driver, nullptr);
    EXPECT_EQ(driver->type, DriverType::BAND_READER);

    BandReader* found = static_cast<BandReader*>(driver->abstractSelf());
    std::vector<BandUid> taps;
    found->setTapHandler([&taps](const BandUid& uid) { taps.push_back(uid); });
    reader->tapBand({0x04, 0xA2, 0x1B, 0x9C});
    ASSERT_EQ(taps.size(), 1u);
    EXPECT_EQ(taps[0].length, 4);
    EXPECT_EQ(taps[0].bytes[3], 0x9C);
    withReader.dismountDrivers();

    DriverManager withoutReader({});
    EXPECT_EQ(withoutReader.getDriver(BAND_READER_DRIVER_NAME), nullptr);
}
