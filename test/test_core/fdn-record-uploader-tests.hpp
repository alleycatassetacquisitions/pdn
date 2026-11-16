#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstring>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/fdn-record-uploader.hpp"
#include "device-mock.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "match-manager-tests.hpp"
#include "quickdraw-match-codec-tests.hpp"
#include "utility-tests.hpp"

class FdnRecordUploaderTests : public testing::Test {
public:
    /** Wires the store and uploader to a radio whose HTTP requests are captured. */
    void SetUp() override {
        SimpleTimer::setPlatformClock(&clock);
        clock.setTime(1);
        ON_CALL(httpClient, queueRequest(testing::_))
        .WillByDefault(testing::Invoke([this](HttpRequest&& request) {
            requests.push_back(std::move(request));
            return true;
        }));
        storage.write(LEGACY_MATCHES_NAMESPACE, "match_0", "{\"match\":1}");
        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        store = new FdnRecordStore(wirelessManager, &storage);
        uploader = new FdnRecordUploader(wirelessManager, store, [this]() { return cabled; });
    }

    /** Frees in reverse order of construction. */
    void TearDown() override {
        delete uploader;
        delete store;
        delete wirelessManager;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /** Stores match @p number from pdnA, its UUID ending in @p number. */
    void storeMatch(uint32_t number) {
        FdnRecordStore::MatchRing::Entry entry{};
        memcpy(entry.senderMac, pdnA, 6);
        entry.kind = static_cast<uint8_t>(RecordKind::MATCH);
        entry.recordNumber = number;
        const MatchRecord record = makeMatchRecord(number);
        memcpy(entry.content, &record, sizeof(record));
        ASSERT_TRUE(store->getMatchRecords().store(entry));
    }

    /** Stores crash @p number from pdnA. */
    void storeCrash(uint32_t number) {
        FdnRecordStore::CrashRing::Entry entry{};
        memcpy(entry.senderMac, pdnA, 6);
        entry.kind = static_cast<uint8_t>(RecordKind::CRASH);
        entry.recordNumber = number;
        CrashPacket packet{};
        packet.crashNumber = number;
        packet.resetReason = 4;
        memcpy(entry.content, &packet, sizeof(packet));
        ASSERT_TRUE(store->getCrashRecords().store(entry));
    }

    /** Answers the newest request with a 2xx body, then lets the uploader act. */
    void succeed(const std::string& body = "") {
        ASSERT_FALSE(requests.empty());
        requests.back().onSuccess(body);
        uploader->sync();
    }

    /** Answers the newest request with an error, then lets the uploader act. */
    void fail(WirelessError code, int httpStatus) {
        ASSERT_FALSE(requests.empty());
        requests.back().onError({code, "test", httpStatus});
        uploader->sync();
    }

    /** @return Matches the store holds. */
    uint32_t storedMatches() { return store->getMatchRecords().getStoredCount(); }

    /** @return Crash records the store holds. */
    uint32_t storedCrashes() { return store->getCrashRecords().getStoredCount(); }

    const uint8_t pdnA[6] = {0x0A, 0x01, 0x02, 0x03, 0x04, 0x05};
    FakePlatformClock clock;
    testing::NiceMock<MockPeerComms> peerComms;
    testing::NiceMock<MockHttpClient> httpClient;
    NativePrefsDriver storage{"fdn-prefs", {FDN_RECORD_STORE_NAMESPACE, LEGACY_MATCHES_NAMESPACE}};
    WirelessManager* wirelessManager = nullptr;
    FdnRecordStore* store = nullptr;
    FdnRecordUploader* uploader = nullptr;
    std::vector<HttpRequest> requests;
    bool cabled = false;
};

TEST_F(FdnRecordUploaderTests, bootCycleSendsAllMatchesInOneBatch) {
    storeMatch(1);
    storeMatch(2);

    uploader->sync();

    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].path, MatchesApi::POST_QUICKDRAW_MATCHES_ENDPOINT);
    const std::vector<alleycat_device_QuickdrawMatch> matches =
    decodeTestRequest(requests[0].payload);
    ASSERT_EQ(matches.size(), 2u);
    EXPECT_STREQ(matches[0].id, "00000000-0000-4000-8000-000000000001");
    EXPECT_STREQ(matches[1].id, "00000000-0000-4000-8000-000000000002");
    EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::WIFI);
}

TEST_F(FdnRecordUploaderTests, noCycleWhenNothingWaits) {
    uploader->sync();
    clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    uploader->sync();

    EXPECT_TRUE(requests.empty());
    EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);
}

TEST_F(FdnRecordUploaderTests, noContentRetiresTheBatchAndReturnsTheRadio) {
    storeMatch(1);
    storeMatch(2);
    uploader->sync();

    succeed();

    EXPECT_EQ(storedMatches(), 0u);
    EXPECT_EQ(requests.size(), 1u);
    EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);
}

TEST_F(FdnRecordUploaderTests, rejectedEntriesRetireNeverRetry) {
    storeMatch(1);
    storeMatch(2);
    uploader->sync();

    succeed(encodeUploadResponse({makeRejectionMessage(
    0, "00000000-0000-4000-8000-000000000001",
    alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_EVENT_REFERENCE)}));
    clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    uploader->sync();

    EXPECT_EQ(storedMatches(), 0u);
    EXPECT_EQ(requests.size(), 1u);
}

TEST_F(FdnRecordUploaderTests, serverErrorConflictAndTimeoutKeep) {
    storeMatch(1);
    const std::vector<std::pair<WirelessError, int>> outcomes = {
    {WirelessError::SERVER_ERROR, 500},
    {WirelessError::INVALID_RESPONSE, 409},
    {WirelessError::TIMEOUT, 0},
    {WirelessError::INVALID_RESPONSE, 0},  // a 2xx body that did not decode
    };
    for (const std::pair<WirelessError, int>& outcome : outcomes) {
        const size_t sent = requests.size();
        if (sent == 0) {
            uploader->sync();
        } else {
            clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
            uploader->sync();
        }
        ASSERT_EQ(requests.size(), sent + 1) << "a new cycle after " << outcome.second;

        fail(outcome.first, outcome.second);

        EXPECT_EQ(storedMatches(), 1u) << "kept after " << outcome.second;
        EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);
        EXPECT_EQ(requests.size(), sent + 1) << "no retry in the same cycle";
    }
}

TEST_F(FdnRecordUploaderTests, refusedBatchProbesTheOldestThenRetiresIt) {
    storeMatch(1);
    storeMatch(2);
    uploader->sync();

    fail(WirelessError::INVALID_RESPONSE, 400);

    ASSERT_EQ(requests.size(), 2u);
    std::vector<alleycat_device_QuickdrawMatch> matches = decodeTestRequest(requests[1].payload);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_STREQ(matches[0].id, "00000000-0000-4000-8000-000000000001");
    EXPECT_EQ(storedMatches(), 2u);

    fail(WirelessError::INVALID_RESPONSE, 413);

    EXPECT_EQ(storedMatches(), 1u);
    ASSERT_EQ(requests.size(), 3u);
    matches = decodeTestRequest(requests[2].payload);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_STREQ(matches[0].id, "00000000-0000-4000-8000-000000000002");
}

TEST_F(FdnRecordUploaderTests, probeSurvivesAKeptAnswerThenEnds) {
    storeMatch(1);
    storeMatch(2);
    storeMatch(3);
    uploader->sync();
    fail(WirelessError::INVALID_RESPONSE, 415);
    fail(WirelessError::SERVER_ERROR, 500);

    clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    uploader->sync();
    ASSERT_EQ(requests.size(), 3u);
    EXPECT_EQ(decodeTestRequest(requests[2].payload).size(), 1u)
    << "next attempt is the oldest alone";

    succeed();

    EXPECT_EQ(storedMatches(), 2u);
    ASSERT_EQ(requests.size(), 4u);
    EXPECT_EQ(decodeTestRequest(requests[3].payload).size(), 2u) << "a retired probe ends probing";
}

TEST_F(FdnRecordUploaderTests, matchesGoBeforeCrashRecordsOldestFirst) {
    storeCrash(5);
    storeCrash(6);
    storeMatch(1);
    uploader->sync();

    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].path, MatchesApi::POST_QUICKDRAW_MATCHES_ENDPOINT);
    succeed();

    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[1].path, CrashApi::POST_DEVICE_LOGS_ENDPOINT);
    alleycat_device_WriteDeviceLogRequest crash = decodeBytes(
    reinterpret_cast<const uint8_t*>(requests[1].payload.data()), requests[1].payload.size());
    EXPECT_EQ(crash.crash_number, 5u);
    EXPECT_STREQ(crash.device_mac, "0a:01:02:03:04:05") << "the PDN the record came from";
    succeed();

    ASSERT_EQ(requests.size(), 3u);
    crash = decodeBytes(reinterpret_cast<const uint8_t*>(requests[2].payload.data()),
                        requests[2].payload.size());
    EXPECT_EQ(crash.crash_number, 6u);
    succeed();

    EXPECT_EQ(storedCrashes(), 0u);
    EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);
}

TEST_F(FdnRecordUploaderTests, refusedCrashRecordRetiresAndKeptOneStays) {
    storeCrash(5);
    storeCrash(6);
    uploader->sync();

    fail(WirelessError::INVALID_RESPONSE, 400);
    EXPECT_EQ(storedCrashes(), 1u);
    ASSERT_EQ(requests.size(), 2u);

    fail(WirelessError::SERVER_ERROR, 503);
    EXPECT_EQ(storedCrashes(), 1u);
    EXPECT_EQ(requests.size(), 2u);
}

TEST_F(FdnRecordUploaderTests, keptMatchesStillLetCrashRecordsGo) {
    storeMatch(1);
    storeCrash(5);
    uploader->sync();

    fail(WirelessError::INVALID_RESPONSE, 409);  // no active event

    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[1].path, CrashApi::POST_DEVICE_LOGS_ENDPOINT);
    succeed();
    EXPECT_EQ(storedCrashes(), 0u);
    EXPECT_EQ(storedMatches(), 1u);
    EXPECT_EQ(requests.size(), 2u) << "the kept matches wait for the next cycle";
}

TEST_F(FdnRecordUploaderTests, noAnswerKeepsEverythingForTheNextCycle) {
    storeMatch(1);
    storeCrash(5);
    uploader->sync();

    fail(WirelessError::WIFI_NOT_CONNECTED, 0);

    EXPECT_EQ(requests.size(), 1u);
    EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);
}

TEST_F(FdnRecordUploaderTests, answerFromAnEarlierCycleIsIgnored) {
    storeMatch(1);
    uploader->sync();
    clock.advance(FDN_UPLOAD_CYCLE_CAP_MS + 1);
    uploader->sync();
    clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    uploader->sync();
    ASSERT_EQ(requests.size(), 2u);

    requests[0].onSuccess("");
    uploader->sync();

    EXPECT_EQ(storedMatches(), 1u) << "the first cycle's answer is stale";
}

TEST_F(FdnRecordUploaderTests, cycleCapReturnsTheRadioAndIgnoresALateAnswer) {
    storeMatch(1);
    uploader->sync();

    clock.advance(FDN_UPLOAD_CYCLE_CAP_MS + 1);
    uploader->sync();
    EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);

    succeed();
    EXPECT_EQ(storedMatches(), 1u) << "kept: the answer came after the cap";
}

TEST_F(FdnRecordUploaderTests, skipsWhileCabledThenUploadsOnceUnplugged) {
    storeMatch(1);
    cabled = true;
    uploader->sync();
    clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    uploader->sync();
    EXPECT_TRUE(requests.empty());
    EXPECT_EQ(wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);

    cabled = false;
    uploader->sync();
    EXPECT_EQ(requests.size(), 1u);
}

TEST_F(FdnRecordUploaderTests, nextCycleWaitsForTheInterval) {
    storeMatch(1);
    uploader->sync();
    fail(WirelessError::SERVER_ERROR, 500);

    clock.advance(FDN_UPLOAD_INTERVAL_MS - 1);
    uploader->sync();
    EXPECT_EQ(requests.size(), 1u);

    clock.advance(2);
    uploader->sync();
    EXPECT_EQ(requests.size(), 2u);
}

TEST_F(FdnRecordUploaderTests, legacyMatchesNamespaceUntouched) {
    storeMatch(1);
    storeCrash(1);
    uploader->sync();
    succeed();
    succeed();

    EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, "match_0", ""), "{\"match\":1}");
}
