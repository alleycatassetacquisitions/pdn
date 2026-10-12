#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <pb_decode.h>

#include <vector>

#include "alleycat-server/device_api.pb.h"
#include "apps/player-registration/player-registration-states.hpp"
#include "device-mock.hpp"
#include "device/crash/crash-record-source.hpp"
#include "game/match-manager.hpp"
#include "utility-tests.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

/** Crash records held in a vector, uploaded in crash-number order. */
class InMemoryCrashRecordSource : public CrashRecordSource {
public:
    /** The lowest-numbered record above the highest one marked uploaded. */
    bool loadOldestPending(CrashRecord& out) override {
        for (const CrashRecord& record : records) {
            if (record.crashNumber > uploadedThrough) {
                out = record;
                return true;
            }
        }
        return false;
    }

    /** Records the call, so tests can see which numbers were confirmed. */
    void markUploaded(uint32_t crashNumber) override {
        uploaded.push_back(crashNumber);
        if (crashNumber > uploadedThrough) {
            uploadedThrough = crashNumber;
        }
    }

    /** A record as a panic with a core dump summary leaves it. */
    void addRecord(uint32_t crashNumber) {
        CrashRecord record{};
        record.crashNumber = crashNumber;
        record.timestamp = 1200 + crashNumber;
        record.resetReason = 4;
        record.programCounter = 0x42001234;
        record.exceptionCause = 28;
        records.push_back(record);
    }

    std::vector<CrashRecord> records;
    std::vector<uint32_t> uploaded;
    uint32_t uploadedThrough = 0;
};

/**
 * Drives FetchUserDataState's player fetch and crash upload through the HTTP
 * requests it queues, answering each one by hand.
 */
class CrashUploadTestSuite : public testing::Test {
public:
    /** Every HTTP request the state queues lands in `queued`, unanswered. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        // Off a multiple of 50ms, so onStateLoop never redraws the loading screen.
        fakeClock->setTime(10001);

        char playerId[] = "1234";
        player.setUserID(playerId);

        quickdrawWirelessManager.initialize(&player, nullptr, 0);
        matchManager.initialize(&player, device.mockStorage, &quickdrawWirelessManager);

        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(mac));
        ON_CALL(*device.mockHttpClient, queueRequest(_)).WillByDefault(Invoke([this](HttpRequest& request) {
            queued.push_back(request);
            return true;
        }));

        state = new FetchUserDataState(&player, device.wirelessManager, nullptr, &matchManager, &source);
    }

    /** Deletes the state and restores the platform clock. */
    void TearDown() override {
        delete state;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** Starts the player fetch and answers it with a registered player. */
    void answerPlayerFetch() {
        state->fetchUserData();
        ASSERT_EQ(queued.size(), 1u);
        queued[0].onSuccess(R"({"data":{"id":"1234","name":"Player1234","hunter":true,"allegiance":1,"faction":"Default"}})");
    }

    /** Decodes the protobuf body of `queued[index]`. */
    alleycat_device_WriteDeviceLogRequest decodeQueued(size_t index) {
        alleycat_device_WriteDeviceLogRequest msg = alleycat_device_WriteDeviceLogRequest_init_zero;
        const std::string& payload = queued[index].payload;
        pb_istream_t stream =
            pb_istream_from_buffer(reinterpret_cast<const pb_byte_t*>(payload.data()), payload.size());
        EXPECT_TRUE(pb_decode(&stream, alleycat_device_WriteDeviceLogRequest_fields, &msg));
        return msg;
    }

    uint8_t mac[6] = {0x24, 0x58, 0x7c, 0x01, 0x02, 0x03};
    FakePlatformClock* fakeClock = nullptr;
    MockDevice device;
    Player player;
    FakeQuickdrawWirelessManager quickdrawWirelessManager;
    MatchManager matchManager;
    InMemoryCrashRecordSource source;
    FetchUserDataState* state = nullptr;
    std::vector<HttpRequest> queued;
};

inline void crashUploadSendsPendingRecordsOldestFirst(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->source.addRecord(2);

    suite->answerPlayerFetch();

    ASSERT_EQ(suite->queued.size(), 2u);
    EXPECT_EQ(suite->queued[1].method, "POST");
    EXPECT_EQ(suite->queued[1].path, "/device-logs");
    EXPECT_EQ(suite->queued[1].contentType, "application/protobuf");
    alleycat_device_WriteDeviceLogRequest first = suite->decodeQueued(1);
    EXPECT_EQ(first.crash_number, 1u);
    EXPECT_STREQ(first.device_mac, "24:58:7c:01:02:03");
    EXPECT_FALSE(suite->state->transitionToWelcomeMessage());

    suite->queued[1].onSuccess("");

    ASSERT_EQ(suite->queued.size(), 3u);
    EXPECT_EQ(suite->decodeQueued(2).crash_number, 2u);
    EXPECT_FALSE(suite->state->transitionToWelcomeMessage());

    suite->queued[2].onSuccess("");

    EXPECT_EQ(suite->source.uploaded, (std::vector<uint32_t>{1, 2}));
    EXPECT_TRUE(suite->state->transitionToWelcomeMessage());
    EXPECT_EQ(suite->queued.size(), 3u);
}

inline void crashUploadServerErrorLeavesRecordPending(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->answerPlayerFetch();
    ASSERT_EQ(suite->queued.size(), 2u);

    suite->queued[1].onError({WirelessError::SERVER_ERROR, "HTTP Error: 500", false});

    EXPECT_TRUE(suite->source.uploaded.empty());
    EXPECT_TRUE(suite->state->transitionToWelcomeMessage());
    EXPECT_FALSE(suite->state->transitionToConfirmOffline());
}

inline void crashUploadWaitsOutARetryingError(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->answerPlayerFetch();
    ASSERT_EQ(suite->queued.size(), 2u);

    suite->queued[1].onError({WirelessError::CONNECTION_FAILED, "ESP_ERR_HTTP_CONNECT", true});

    EXPECT_FALSE(suite->state->transitionToWelcomeMessage());

    suite->queued[1].onSuccess("");

    EXPECT_EQ(suite->source.uploaded, (std::vector<uint32_t>{1}));
    EXPECT_TRUE(suite->state->transitionToWelcomeMessage());
}

inline void crashUploadTimeoutMovesOnAndLateSuccessStillCounts(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->source.addRecord(2);
    suite->answerPlayerFetch();
    ASSERT_EQ(suite->queued.size(), 2u);

    suite->fakeClock->advance(15001);
    suite->state->onStateLoop(&suite->device);

    EXPECT_TRUE(suite->state->transitionToWelcomeMessage());
    EXPECT_FALSE(suite->state->transitionToConfirmOffline());

    suite->queued[1].onSuccess("");

    EXPECT_EQ(suite->source.uploaded, (std::vector<uint32_t>{1}));
    EXPECT_EQ(suite->queued.size(), 2u);
}

inline void crashUploadWithNothingPendingWelcomesAtOnce(CrashUploadTestSuite* suite) {
    suite->answerPlayerFetch();

    EXPECT_EQ(suite->queued.size(), 1u);
    EXPECT_TRUE(suite->state->transitionToWelcomeMessage());
}

inline void crashUploadNotStartedByAFetchAnsweredAfterDismount(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->state->fetchUserData();
    ASSERT_EQ(suite->queued.size(), 1u);

    // The fetch timed out and the state moved on; the request stays queued in
    // the HTTP driver and is answered once the AP returns.
    suite->state->onStateDismounted(&suite->device);
    suite->queued[0].onSuccess(R"({"data":{"id":"1234","name":"Player1234","hunter":true,"allegiance":1,"faction":"Default"}})");

    EXPECT_EQ(suite->queued.size(), 1u);
    EXPECT_TRUE(suite->source.uploaded.empty());
    EXPECT_FALSE(suite->state->transitionToWelcomeMessage());
}

inline void crashUploadNotStartedByALateMatchUploadAnswer(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->state->uploadMatches();
    ASSERT_EQ(suite->queued.size(), 1u);

    suite->state->onStateDismounted(&suite->device);
    suite->queued[0].onSuccess(R"({"success":true})");

    EXPECT_EQ(suite->queued.size(), 1u);
    EXPECT_FALSE(suite->state->transitionToWelcomeMessage());
}

inline void crashUploadIgnoresAnAnswerFromAnEarlierMount(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->state->fetchUserData();
    suite->state->onStateDismounted(&suite->device);
    suite->state->fetchUserData();
    ASSERT_EQ(suite->queued.size(), 2u);

    suite->queued[0].onSuccess(R"({"data":{"id":"1234","name":"Player1234","hunter":true,"allegiance":1,"faction":"Default"}})");

    EXPECT_EQ(suite->queued.size(), 2u);
    EXPECT_FALSE(suite->state->transitionToWelcomeMessage());

    suite->queued[1].onSuccess(R"({"data":{"id":"1234","name":"Player1234","hunter":true,"allegiance":1,"faction":"Default"}})");

    ASSERT_EQ(suite->queued.size(), 3u);
    EXPECT_EQ(suite->queued[2].path, "/device-logs");
}

inline void crashUploadSkippedWhenPlayerFetchFails(CrashUploadTestSuite* suite) {
    suite->source.addRecord(1);
    suite->state->fetchUserData();
    ASSERT_EQ(suite->queued.size(), 1u);

    suite->queued[0].onError({WirelessError::WIFI_NOT_CONNECTED, "WiFi lost", false});

    EXPECT_EQ(suite->queued.size(), 1u);
    EXPECT_TRUE(suite->source.uploaded.empty());
    EXPECT_TRUE(suite->state->transitionToConfirmOffline());
}
