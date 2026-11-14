#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "alleycat-server/device_api.pb.h"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "alleycat-server/fdn-record-store.hpp"
#include "alleycat-server/fdn-record-uploader.hpp"
#include "alleycat-server/fdn-stored-entry.hpp"
#include "crash-codec-tests.hpp"
#include "device/crash/crash-record.hpp"
#include "device-mock.hpp"
#include "fdn-record-store-tests.hpp"
#include "game/player-types.hpp"
#include "quickdraw-match-codec-tests.hpp"
#include "utility-tests.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/record-source.hpp"
#include "wireless/wireless-types.hpp"

/**
 * The FDN's own crash records above a sent mark. Record N is a real crash
 * record with crash number N and no core dump details.
 */
class FakeOwnCrashSource : public RecordSource<CrashRecordTraits> {
public:
    /** Hands out record mark + 1 while it is not above the newest. */
    bool readLowestPending(uint32_t& recordNumber, CrashRecordContent& content) override {
        if (mark >= newestNumber) return false;
        recordNumber = mark + 1;
        CrashRecord record{};
        record.crashNumber = recordNumber;
        record.timestamp = 1000 * recordNumber;
        record.resetReason = 4;
        strncpy(record.commitHash, "fd0c0de1", COMMIT_HASH_LENGTH - 1);
        const CrashPacket packet = toCrashPacket(record);
        memcpy(content.recordBytes, &packet, sizeof(packet));
        return true;
    }

    /**
     * Moves the mark forwards only; not at all while markWritesFail, as when
     * the storage behind a real mark refuses the write.
     */
    void advanceMark(uint32_t recordNumber) override {
        if (markWritesFail) return;
        if (recordNumber > mark && recordNumber <= newestNumber) mark = recordNumber;
    }

    uint32_t newestNumber = 0;
    uint32_t mark = 0;
    bool markWritesFail = false;
};

/**
 * The FDN of FdnRecordStoreTests with a record uploader over its store, a fake
 * source of its own crash records, and an HTTP client that behaves as the
 * drivers do: a request stays queued until the test answers it, and leaving
 * Wi-Fi fails every request still queued through its onError, one queued by
 * such a failure included.
 */
class FdnRecordUploaderTests : public FdnRecordStoreTests {
public:
    /** Builds the uploader over the store, with the FDN's own MAC address on the radio. */
    void SetUp() override {
        FdnRecordStoreTests::SetUp();
        MockHttpClient& httpClient = *fdnDevice.mockHttpClient;
        ON_CALL(*fdnDevice.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(fdnMac));
        ON_CALL(httpClient, queueRequest(testing::_)).WillByDefault([this](HttpRequest&& request) {
            if (refuseRequests) return false;
            httpRequests.push_back(std::move(request));
            answered.push_back(false);
            return true;
        });
        ON_CALL(httpClient, hasQueuedRequests()).WillByDefault([this]() {
            return hasUnansweredRequest();
        });
        ON_CALL(httpClient, getHttpClientState()).WillByDefault([this]() {
            return httpClientState;
        });
        ON_CALL(httpClient, setHttpClientState(testing::_))
        .WillByDefault([this](HttpClientState state) {
            httpClientState = state;
            if (state == HttpClientState::DISCONNECTED) failEveryQueuedRequest();
        });
        fdnRecordUploader =
        new FdnRecordUploader(fdnDevice.wirelessManager, &ownCrashSource, fdnRecordStore);
    }

    /** Releases the uploader before the store it reads. */
    void TearDown() override {
        delete fdnRecordUploader;
        fdnRecordUploader = nullptr;
        httpRequests.clear();
        FdnRecordStoreTests::TearDown();
    }

    /**
     * A crash record frame carrying a real crash record with crash number
     * @p crashNumber and core dump details.
     */
    static CrashRecordFramePacket relayedCrashFrame(uint32_t crashNumber) {
        CrashRecord record{};
        record.crashNumber = crashNumber;
        record.timestamp = 12500 + crashNumber;
        record.resetReason = 7;
        strncpy(record.commitHash, "a1b2c3d4", COMMIT_HASH_LENGTH - 1);
        CoreDumpDetails& details = record.coreDumpDetails.emplace();
        details.programCounter = 0x400D1A2C;
        details.exceptionCause = 28;
        strncpy(details.taskName, "loopTask", TASK_NAME_LENGTH - 1);
        const CrashPacket packet = toCrashPacket(record);
        CrashRecordFramePacket frame = crashFrame(crashNumber, 0);
        memcpy(frame.content.recordBytes, &packet, sizeof(packet));
        return frame;
    }

    /** The device log the captured request at @p index carries. */
    alleycat_device_WriteDeviceLogRequest deviceLogOf(size_t index) const {
        const std::string& payload = httpRequests[index].payload;
        return decodeBytes(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
    }

    /**
     * Answers the captured request at @p index with HTTP 204. The callback is
     * copied out first: it may queue the next request, which moves the
     * captured ones while it runs.
     */
    void answerSuccess(size_t index) {
        if (!takeFromQueue(index)) return;
        const HttpSuccessCallback onSuccess = httpRequests[index].onSuccess;
        onSuccess("");
    }

    /** Answers the captured request at @p index with the HTTP error status @p httpStatus. */
    void answerHttpError(size_t index, int httpStatus) {
        if (!takeFromQueue(index)) return;
        const HttpErrorCallback onError = httpRequests[index].onError;
        WirelessErrorInfo error{};
        error.code =
        httpStatus >= 500 ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE;
        error.message = "HTTP " + std::to_string(httpStatus);
        error.httpStatus = httpStatus;
        onError(error);
    }

    /** Fails the captured request at @p index as a lost connection does: no answer at all. */
    void failWithoutAnswer(size_t index) {
        if (!takeFromQueue(index)) return;
        const HttpErrorCallback onError = httpRequests[index].onError;
        onError({WirelessError::CONNECTION_FAILED, "Connection lost"});
    }

    /** True while a captured request has had neither an answer nor a failure. */
    bool hasUnansweredRequest() const {
        for (const bool isAnswered : answered) {
            if (!isAnswered) return true;
        }
        return false;
    }

    /** True while the radio is on peer-to-peer, false while HTTP holds it on Wi-Fi. */
    bool radioIsPeerToPeer() {
        return fdnDevice.wirelessManager->getCurrentMode() == WirelessMode::ESPNOW;
    }

    /** Relayed crash records the store holds. */
    size_t storedCrashCount() const { return fdnRecordStore->getStoredCount(RecordKind::CRASH); }

    /** Relayed match records the store holds. */
    size_t storedMatchCount() const { return fdnRecordStore->getStoredCount(RecordKind::MATCH); }

    FakeOwnCrashSource ownCrashSource;
    FdnRecordUploader* fdnRecordUploader = nullptr;
    uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::vector<HttpRequest> httpRequests;
    bool refuseRequests = false;

private:
    // A request has its callback once, as from a driver that popped it first.
    bool takeFromQueue(size_t index) {
        if (index >= httpRequests.size() || answered[index]) {
            ADD_FAILURE() << "request " << index << " is not waiting for an answer";
            return false;
        }
        answered[index] = true;
        return true;
    }

    // By index and to the end as it grows: a callback may queue another
    // request, which moves the captured ones and is failed in the same pass.
    void failEveryQueuedRequest() {
        for (size_t index = 0; index < httpRequests.size(); index++) {
            if (answered[index]) continue;
            answered[index] = true;
            const HttpErrorCallback onError = httpRequests[index].onError;
            onError({WirelessError::CONNECTION_FAILED, "Disconnected before an answer"});
        }
    }

    std::vector<bool> answered;
    HttpClientState httpClientState = HttpClientState::DISCONNECTED;
};

// ---- Relayed crash records ----

inline void fdnUploadsARelayedCrashRecordUnderTheOriginatingPdnsMac(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(7));
    ASSERT_EQ(suite->storedCrashCount(), 1u);

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_EQ(suite->httpRequests[0].path, "/device-logs");
    EXPECT_EQ(suite->httpRequests[0].method, "POST");
    EXPECT_EQ(suite->httpRequests[0].contentType, "application/protobuf");

    const alleycat_device_WriteDeviceLogRequest log = suite->deviceLogOf(0);
    EXPECT_TRUE(log.has_device_mac);
    EXPECT_STREQ(log.device_mac, "aa:bb:cc:dd:ee:31")
    << "the upload must name the PDN that crashed, not the FDN that relays";
    EXPECT_TRUE(log.has_crash_number);
    EXPECT_EQ(log.crash_number, 7u);
    EXPECT_TRUE(log.has_uptime_ms);
    EXPECT_EQ(log.uptime_ms, 12507u);
    EXPECT_TRUE(log.has_software_version);
    EXPECT_STREQ(log.software_version, "a1b2c3d4");
    EXPECT_TRUE(log.has_reset_reason);
    EXPECT_EQ(log.reset_reason, 7u);
    EXPECT_TRUE(log.has_program_counter);
    EXPECT_EQ(log.program_counter, 0x400D1A2Cu);
    EXPECT_TRUE(log.has_exception_cause);
    EXPECT_EQ(log.exception_cause, 28u);
    EXPECT_TRUE(log.has_task_name);
    EXPECT_STREQ(log.task_name, "loopTask");

    EXPECT_EQ(suite->storedCrashCount(), 1u) << "a record is retired only on the server's answer";
    suite->answerSuccess(0);
    EXPECT_EQ(suite->storedCrashCount(), 0u);
    EXPECT_EQ(suite->httpRequests.size(), 1u);

    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_TRUE(suite->radioIsPeerToPeer());
}

// Each of these answers says the server refused the body itself, so the same
// record would be refused on every later uplink and block the ones behind it.
inline void
fdnRetiresARelayedCrashRecordWhoseBodyWasRefusedAndUploadsTheNext(FdnRecordUploaderTests* suite) {
    size_t requestIndex = 0;
    uint32_t crashNumber = 1;
    for (const int httpStatus : {400, 413, 415, 422}) {
        suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                       FdnRecordUploaderTests::relayedCrashFrame(crashNumber));
        suite->deliver(PktType::kCrashRecord, suite->otherPdnMac,
                       FdnRecordUploaderTests::relayedCrashFrame(crashNumber + 1));
        ASSERT_EQ(suite->storedCrashCount(), 2u);

        ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords()) << "HTTP " << httpStatus;
        ASSERT_EQ(suite->httpRequests.size(), requestIndex + 1)
        << "crash records go up one request at a time";
        EXPECT_EQ(suite->deviceLogOf(requestIndex).crash_number, crashNumber)
        << "the oldest record goes first";

        suite->answerHttpError(requestIndex, httpStatus);
        EXPECT_EQ(suite->storedCrashCount(), 1u)
        << "HTTP " << httpStatus << ": a record the server will never take must leave the queue";
        ASSERT_EQ(suite->httpRequests.size(), requestIndex + 2)
        << "HTTP " << httpStatus << ": the upload must continue with the next record";
        EXPECT_EQ(suite->deviceLogOf(requestIndex + 1).crash_number, crashNumber + 1)
        << "HTTP " << httpStatus << ": the refused record was sent again";
        EXPECT_STREQ(suite->deviceLogOf(requestIndex + 1).device_mac, "aa:bb:cc:dd:ee:32");

        suite->answerSuccess(requestIndex + 1);
        EXPECT_EQ(suite->storedCrashCount(), 0u);
        suite->fdnRecordUploader->sync();
        EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail())
        << "HTTP " << httpStatus << " is the server's answer, not a failed uplink";
        EXPECT_TRUE(suite->radioIsPeerToPeer());
        EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());

        requestIndex += 2;
        crashNumber += 2;
    }

    EXPECT_FALSE(suite->fdnRecordUploader->uploadAllRecords());
    EXPECT_EQ(suite->httpRequests.size(), requestIndex)
    << "a refused record came back on a later uplink";
}

inline void fdnLeavesRelayedCrashRecordsQueuedOn503(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));
    suite->deliver(PktType::kCrashRecord, suite->otherPdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(2));

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    suite->answerHttpError(0, 503);
    EXPECT_EQ(suite->storedCrashCount(), 2u) << "a failed record and those behind it stay queued";
    EXPECT_EQ(suite->httpRequests.size(), 1u) << "the upload must stop at the first failure";

    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail());

    // The next cycle starts again at the record that failed.
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->deviceLogOf(1).crash_number, 1u);
    EXPECT_STREQ(suite->deviceLogOf(1).device_mac, "aa:bb:cc:dd:ee:31");
}

// Only an answer that refuses the body is final; every other failure may be
// the server's trouble or the network's.
inline void fdnKeepsARelayedCrashRecordOnEveryOtherFailure(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));

    size_t requestIndex = 0;
    for (const int httpStatus : {401, 404, 408, 414, 421, 423, 429, 500}) {
        ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords()) << "HTTP " << httpStatus;
        ASSERT_EQ(suite->httpRequests.size(), requestIndex + 1);
        suite->answerHttpError(requestIndex, httpStatus);
        EXPECT_EQ(suite->storedCrashCount(), 1u) << "HTTP " << httpStatus << " retired the record";
        suite->fdnRecordUploader->sync();
        EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail()) << "HTTP " << httpStatus;
        requestIndex++;
    }

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    suite->failWithoutAnswer(requestIndex);
    EXPECT_EQ(suite->storedCrashCount(), 1u) << "a lost connection retired the record";
}

inline void fdnUploadSkipsAnUnreadableRelayedCrashRecord(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(2));
    const uint8_t torn[3] = {1, 2, 3};
    suite->fdnPrefs.writeBytes(FDN_RECORD_STORE_NAMESPACE, "c_0", torn, sizeof(torn));

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u) << "the readable record still goes up";
    EXPECT_EQ(suite->deviceLogOf(0).crash_number, 2u);
    EXPECT_EQ(suite->storedCrashCount(), 1u) << "the torn record must not block the queue";
}

// ---- When the uplink starts ----

inline void fdnUplinkStartsWithOnlyCrashRecordsStaged(FdnRecordUploaderTests* suite) {
    EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());

    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));
    ASSERT_EQ(suite->storedMatchCount(), 0u);
    EXPECT_TRUE(suite->fdnRecordUploader->hasRecordsWaiting())
    << "a relayed crash record alone must start the uplink";

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_EQ(suite->httpRequests[0].path, "/device-logs");
}

inline void fdnUplinkStartsWithOnlyAnOwnCrashRecordPending(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 1;
    EXPECT_TRUE(suite->fdnRecordUploader->hasRecordsWaiting())
    << "a crash record of the FDN's own must start the uplink";

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_EQ(suite->httpRequests[0].path, "/device-logs");
    EXPECT_STREQ(suite->deviceLogOf(0).device_mac, "fd:00:00:00:00:01");
}

inline void fdnUplinkStartsWithOnlyMatchRecordsStaged(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    EXPECT_TRUE(suite->fdnRecordUploader->hasRecordsWaiting());

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_EQ(suite->httpRequests[0].path, "/quickdraw-matches");
    suite->answerSuccess(0);
    EXPECT_EQ(suite->storedMatchCount(), 0u);
    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_TRUE(suite->radioIsPeerToPeer());
}

inline void fdnUplinkDoesNotStartWithNothingStaged(FdnRecordUploaderTests* suite) {
    EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());

    EXPECT_FALSE(suite->fdnRecordUploader->uploadAllRecords());
    EXPECT_TRUE(suite->httpRequests.empty());
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_TRUE(suite->radioIsPeerToPeer())
    << "with nothing to upload the radio must stay on peer-to-peer";
    EXPECT_TRUE(suite->fdnDevice.wirelessManager->acquireWifi(suite))
    << "a cycle that sent nothing kept Wi-Fi from everything else";
}

inline void fdnRunsOneUploadCycleAtATime(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 1;
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);

    EXPECT_FALSE(suite->fdnRecordUploader->uploadAllRecords());
    EXPECT_EQ(suite->httpRequests.size(), 1u) << "a second cycle sent the same record again";
    EXPECT_TRUE(suite->fdnRecordUploader->isUploading());
}

// ---- The FDN's own crash records ----

inline void fdnUploadsItsOwnCrashRecordsInOrderAndAdvancesTheMark(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 2;

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_EQ(suite->httpRequests[0].path, "/device-logs");
    const alleycat_device_WriteDeviceLogRequest first = suite->deviceLogOf(0);
    EXPECT_STREQ(first.device_mac, "fd:00:00:00:00:01");
    EXPECT_EQ(first.crash_number, 1u);
    EXPECT_EQ(first.uptime_ms, 1000u);
    EXPECT_STREQ(first.software_version, "fd0c0de1");
    EXPECT_EQ(first.reset_reason, 4u);
    EXPECT_FALSE(first.has_program_counter);
    EXPECT_FALSE(first.has_exception_cause);
    EXPECT_FALSE(first.has_task_name);
    EXPECT_EQ(suite->ownCrashSource.mark, 0u) << "the mark moves only on the server's answer";
    EXPECT_FALSE(suite->radioIsPeerToPeer());

    suite->answerSuccess(0);
    EXPECT_EQ(suite->ownCrashSource.mark, 1u);
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->deviceLogOf(1).crash_number, 2u);
    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->fdnRecordUploader->isUploading());
    EXPECT_FALSE(suite->radioIsPeerToPeer()) << "the radio left Wi-Fi with a request unanswered";

    suite->answerSuccess(1);
    EXPECT_EQ(suite->ownCrashSource.mark, 2u);
    EXPECT_EQ(suite->httpRequests.size(), 2u);
    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_TRUE(suite->radioIsPeerToPeer())
    << "the radio must return to peer-to-peer once the requests are answered";
}

// The boot upload is one cycle; the periodic uplink is every cycle after it.
inline void
fdnUploadsRelayedCrashRecordsOnTheUplinkAfterTheBootUpload(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 1;
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    suite->answerSuccess(0);
    suite->fdnRecordUploader->sync();
    ASSERT_TRUE(suite->radioIsPeerToPeer());
    EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());

    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));
    EXPECT_TRUE(suite->fdnRecordUploader->hasRecordsWaiting())
    << "a relayed crash record must start the periodic uplink";
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords())
    << "the boot upload latched out every later upload";
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_STREQ(suite->deviceLogOf(1).device_mac, "aa:bb:cc:dd:ee:31");
    EXPECT_EQ(suite->deviceLogOf(1).crash_number, 1u);
    suite->answerSuccess(1);
    EXPECT_EQ(suite->storedCrashCount(), 0u);
}

// A reboot does not empty the store, and the boot upload is a whole cycle.
inline void fdnBootUploadSendsTheRelayedRecordsHeldAcrossAReboot(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(3));
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    delete suite->fdnRecordUploader;
    suite->restartStore();
    suite->fdnRecordUploader = new FdnRecordUploader(suite->fdnDevice.wirelessManager,
                                                     &suite->ownCrashSource, suite->fdnRecordStore);

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_EQ(suite->httpRequests[0].path, "/quickdraw-matches");
    suite->answerSuccess(0);
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_STREQ(suite->deviceLogOf(1).device_mac, "aa:bb:cc:dd:ee:31");
    EXPECT_EQ(suite->deviceLogOf(1).crash_number, 3u);
    suite->answerSuccess(1);
    EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());
}

inline void fdnOwnCrashRecordThatFailedGoesUpOnTheNextCycle(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 1;

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    suite->failWithoutAnswer(0);
    EXPECT_EQ(suite->ownCrashSource.mark, 0u);
    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_TRUE(suite->radioIsPeerToPeer());

    EXPECT_TRUE(suite->fdnRecordUploader->hasRecordsWaiting())
    << "the record that failed must start the next uplink";
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->deviceLogOf(1).crash_number, 1u);
    EXPECT_STREQ(suite->deviceLogOf(1).device_mac, "fd:00:00:00:00:01");

    suite->answerSuccess(1);
    EXPECT_EQ(suite->ownCrashSource.mark, 1u);
    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());
}

// Nothing retires a record of the FDN's own but the server taking it, so one
// the server keeps refusing must not hold back what PDNs relayed.
inline void
fdnOwnCrashFailureKeepsTheRecordAndStillUploadsTheRelayedOnes(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 2;

    size_t requestIndex = 0;
    uint32_t relayedNumber = 1;
    for (const int httpStatus : {400, 422, 503}) {
        suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                       FdnRecordUploaderTests::relayedCrashFrame(relayedNumber));
        ASSERT_EQ(suite->storedCrashCount(), 1u);

        ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords()) << "HTTP " << httpStatus;
        ASSERT_EQ(suite->httpRequests.size(), requestIndex + 1)
        << "the FDN's own records are tried first, one request at a time";
        EXPECT_STREQ(suite->deviceLogOf(requestIndex).device_mac, "fd:00:00:00:00:01");
        EXPECT_EQ(suite->deviceLogOf(requestIndex).crash_number, 1u);

        suite->answerHttpError(requestIndex, httpStatus);
        EXPECT_EQ(suite->ownCrashSource.mark, 0u) << "HTTP " << httpStatus << " moved the mark";
        ASSERT_EQ(suite->httpRequests.size(), requestIndex + 2)
        << "HTTP " << httpStatus << " on an own record stopped the relayed ones";
        EXPECT_STREQ(suite->deviceLogOf(requestIndex + 1).device_mac, "aa:bb:cc:dd:ee:31")
        << "HTTP " << httpStatus << ": the upload went on to the own record behind the failed one";
        EXPECT_EQ(suite->deviceLogOf(requestIndex + 1).crash_number, relayedNumber);

        suite->answerSuccess(requestIndex + 1);
        EXPECT_EQ(suite->storedCrashCount(), 0u);
        EXPECT_EQ(suite->httpRequests.size(), requestIndex + 2);
        suite->fdnRecordUploader->sync();
        EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail())
        << "HTTP " << httpStatus << ": a cycle with a failed upload is a failed one";
        EXPECT_TRUE(suite->fdnRecordUploader->hasRecordsWaiting());

        requestIndex += 2;
        relayedNumber++;
    }
}

// The server took the record, but with the mark where it was the record is
// still pending: sent again at once it would repeat until the cycle timed out.
inline void fdnDoesNotRepeatAnOwnCrashRecordWhoseMarkCannotMove(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 1;
    suite->ownCrashSource.markWritesFail = true;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(5));

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    suite->answerSuccess(0);

    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_STREQ(suite->deviceLogOf(1).device_mac, "aa:bb:cc:dd:ee:31")
    << "the record whose mark did not move was sent again in the same cycle";
    suite->answerSuccess(1);
    EXPECT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->storedCrashCount(), 0u);
    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail())
    << "a record that will be uploaded again every cycle was reported as done";

    // Once the mark can move, the one repeat retires the record.
    suite->ownCrashSource.markWritesFail = false;
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 3u);
    EXPECT_EQ(suite->deviceLogOf(2).crash_number, 1u);
    suite->answerSuccess(2);
    EXPECT_EQ(suite->ownCrashSource.mark, 1u);
    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());
}

// ---- The radio ----

inline void fdnRestoresTheRadioAfterAFailedUpload(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));
    ASSERT_TRUE(suite->radioIsPeerToPeer());

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    EXPECT_FALSE(suite->radioIsPeerToPeer());
    suite->answerHttpError(0, 503);

    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->radioIsPeerToPeer())
    << "the radio must return to peer-to-peer after a failed upload";
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
}

inline void fdnRestoresTheRadioWhenTheUploadTimesOut(FdnRecordUploaderTests* suite) {
    suite->ownCrashSource.newestNumber = 2;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));

    // The request is never answered, so it stays queued in the HTTP client.
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);

    suite->fakeClock->advance(15000);
    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->fdnRecordUploader->isUploading());
    EXPECT_FALSE(suite->radioIsPeerToPeer()) << "the upload was cut short of its 15,000 ms";

    suite->fakeClock->advance(1);
    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->radioIsPeerToPeer())
    << "the radio must return to peer-to-peer when the upload times out";
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail())
    << "an upload the timeout failed makes the cycle a failed one";
    EXPECT_EQ(suite->ownCrashSource.mark, 0u) << "an unconfirmed record must stay pending";
    EXPECT_EQ(suite->storedCrashCount(), 1u) << "the timeout retired a relayed record";
    EXPECT_EQ(suite->httpRequests.size(), 1u)
    << "a request was queued while the radio was leaving Wi-Fi";
    EXPECT_FALSE(suite->hasUnansweredRequest())
    << "the timed-out cycle left a request queued with the radio off Wi-Fi";

    // The next cycle sends the record again.
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->deviceLogOf(1).crash_number, 1u);
    EXPECT_STREQ(suite->deviceLogOf(1).device_mac, "fd:00:00:00:00:01");
    suite->answerSuccess(1);
    EXPECT_EQ(suite->ownCrashSource.mark, 1u);
}

inline void fdnRestoresTheRadioWhenARequestCannotBeQueued(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));
    suite->refuseRequests = true;

    // Asking for the request already took the radio to Wi-Fi.
    EXPECT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->radioIsPeerToPeer())
    << "the radio stayed on Wi-Fi after a request that was never queued";
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_EQ(suite->storedCrashCount(), 1u);
}

// ---- Wi-Fi shared with a PIN lookup: one owner at a time ----

/** Cables a PDN to @p rdc and hands the FDN its provision request, which starts a lookup. */
inline void startPinLookup(FdnRecordUploaderTests* suite, FakeRemoteDeviceCoordinator& rdc) {
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    rdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
    rdc.setPeerMac(SerialIdentifier::INPUT_JACK, suite->pdnMac);
    PlayerProvisionRequestPacket request{};
    strncpy(request.pin, "1234", sizeof(request.pin) - 1);
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(
    suite->pdnMac, PktType::kPlayerProvisionRequest, reinterpret_cast<const uint8_t*>(&request),
    sizeof(request));
}

// Leaving Wi-Fi fails every queued request, so a lookup must not start one
// under an upload cycle; and its reply can only go out on peer-to-peer.
inline void
fdnLookupArrivingMidCycleStartsWhenTheCycleEndsAndItsReplyIsSent(FdnRecordUploaderTests* suite) {
    int replyFrames = 0;
    ON_CALL(*suite->fdnDevice.mockPeerComms,
            sendData(testing::_, PktType::kPlayerProvision, testing::_, testing::_))
    .WillByDefault([&replyFrames](const uint8_t*, PktType, const uint8_t*, const size_t) {
        replyFrames++;
        return 1;
    });
    FakeRemoteDeviceCoordinator rdc;
    FdnProvisioningManager fdnProvisioningManager(suite->fdnDevice.wirelessManager, &rdc);
    suite->ownCrashSource.newestNumber = 1;
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);

    startPinLookup(suite, rdc);
    fdnProvisioningManager.sync();
    EXPECT_TRUE(fdnProvisioningManager.isLookupInFlight());
    EXPECT_EQ(suite->httpRequests.size(), 1u) << "the lookup queued a request under the cycle";

    suite->answerSuccess(0);
    EXPECT_EQ(suite->ownCrashSource.mark, 1u) << "the lookup failed the cycle's upload";
    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail());
    ASSERT_TRUE(suite->radioIsPeerToPeer());

    fdnProvisioningManager.sync();
    ASSERT_EQ(suite->httpRequests.size(), 2u) << "the waiting lookup did not start after the cycle";
    EXPECT_EQ(suite->httpRequests[1].method, "GET");
    EXPECT_EQ(replyFrames, 0);

    suite->answerHttpError(1, 404);
    EXPECT_FALSE(fdnProvisioningManager.isLookupInFlight());
    EXPECT_TRUE(suite->radioIsPeerToPeer());
    EXPECT_EQ(replyFrames, 1)
    << "the reply was sent before the radio was back on peer-to-peer, where it is refused";
}

inline void
fdnCycleDoesNotStartWhileALookupIsInFlightAndStartsAtTheNextCheck(FdnRecordUploaderTests* suite) {
    FakeRemoteDeviceCoordinator rdc;
    FdnProvisioningManager fdnProvisioningManager(suite->fdnDevice.wirelessManager, &rdc);
    startPinLookup(suite, rdc);
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    ASSERT_TRUE(fdnProvisioningManager.isLookupInFlight());

    suite->ownCrashSource.newestNumber = 1;
    EXPECT_FALSE(suite->fdnRecordUploader->uploadAllRecords())
    << "an upload cycle started under a lookup";
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_EQ(suite->httpRequests.size(), 1u);
    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->radioIsPeerToPeer()) << "the refused cycle took the radio from the lookup";
    EXPECT_TRUE(suite->hasUnansweredRequest()) << "the refused cycle failed the lookup";
    EXPECT_TRUE(suite->fdnRecordUploader->hasRecordsWaiting());

    suite->answerHttpError(0, 404);
    ASSERT_TRUE(suite->radioIsPeerToPeer());
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords())
    << "the cycle did not start once the lookup had ended";
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->deviceLogOf(1).crash_number, 1u);
}

inline void fdnLookupAloneLeavesWifiWhenItEnds(FdnRecordUploaderTests* suite) {
    FakeRemoteDeviceCoordinator rdc;
    FdnProvisioningManager fdnProvisioningManager(suite->fdnDevice.wirelessManager, &rdc);
    startPinLookup(suite, rdc);
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_FALSE(suite->radioIsPeerToPeer());

    fdnProvisioningManager.cancelLookup();
    EXPECT_TRUE(suite->radioIsPeerToPeer()) << "a cancelled lookup must give Wi-Fi up at once";
    EXPECT_FALSE(fdnProvisioningManager.isLookupInFlight());

    startPinLookup(suite, rdc);
    ASSERT_EQ(suite->httpRequests.size(), 2u) << "the cancelled lookup kept Wi-Fi from the next";
    suite->answerHttpError(1, 404);
    EXPECT_TRUE(suite->radioIsPeerToPeer()) << "a finished lookup stayed on Wi-Fi";
}

// A lookup waiting for Wi-Fi has no request out, so the cycle's deadline has
// nothing of its to fail.
inline void fdnUploadDeadlineLeavesAWaitingLookupToStart(FdnRecordUploaderTests* suite) {
    FakeRemoteDeviceCoordinator rdc;
    FdnProvisioningManager fdnProvisioningManager(suite->fdnDevice.wirelessManager, &rdc);
    suite->ownCrashSource.newestNumber = 1;
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    suite->fakeClock->advance(10000);
    startPinLookup(suite, rdc);

    suite->fakeClock->advance(5001);
    suite->fdnRecordUploader->sync();
    ASSERT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_TRUE(fdnProvisioningManager.isLookupInFlight())
    << "the upload deadline ended a lookup that had not started";

    fdnProvisioningManager.sync();
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->httpRequests[1].method, "GET");
}

inline void fdnLookupThatNeverGetsWifiFailsAtItsOwnTimeLimit(FdnRecordUploaderTests* suite) {
    std::vector<std::string> results;
    FakeRemoteDeviceCoordinator rdc;
    FdnProvisioningManager fdnProvisioningManager(suite->fdnDevice.wirelessManager, &rdc);
    fdnProvisioningManager.setOnLookupDone(
    [&results](ProvisionLookupResult result, const char* name) {
        results.push_back(result == ProvisionLookupResult::LOOKUP_FAILED ? "<failed>" : name);
    });
    suite->ownCrashSource.newestNumber = 1;
    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    startPinLookup(suite, rdc);

    suite->fakeClock->advance(PROVISION_LOOKUP_TIMEOUT_MS);
    fdnProvisioningManager.sync();
    EXPECT_TRUE(fdnProvisioningManager.isLookupInFlight());
    suite->fakeClock->advance(1);
    fdnProvisioningManager.sync();
    EXPECT_FALSE(fdnProvisioningManager.isLookupInFlight());
    EXPECT_EQ(results, (std::vector<std::string>{"<failed>"}));
    EXPECT_EQ(suite->httpRequests.size(), 1u);
    EXPECT_TRUE(suite->hasUnansweredRequest())
    << "the lookup's time limit failed the cycle's upload";
    EXPECT_FALSE(suite->radioIsPeerToPeer());
}

// ---- Every kind in one cycle ----

// The match batch goes first, then the FDN's own crash records, then the
// relayed ones, each when the one before it has nothing more to send.
inline void fdnUploadsEveryKindInOneCycleInOrder(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    suite->deliver(PktType::kCrashRecord, suite->otherPdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(5));
    suite->ownCrashSource.newestNumber = 1;

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u) << "one request is out at a time";
    EXPECT_EQ(suite->httpRequests[0].path, "/quickdraw-matches")
    << "the match batch must not wait behind the crash records";

    suite->answerSuccess(0);
    EXPECT_EQ(suite->storedMatchCount(), 0u);
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->httpRequests[1].path, "/device-logs");
    EXPECT_STREQ(suite->deviceLogOf(1).device_mac, "fd:00:00:00:00:01");
    suite->fdnRecordUploader->sync();
    EXPECT_TRUE(suite->fdnRecordUploader->isUploading())
    << "the cycle ended with the crash records still to go";

    suite->answerSuccess(1);
    EXPECT_EQ(suite->ownCrashSource.mark, 1u);
    ASSERT_EQ(suite->httpRequests.size(), 3u);
    EXPECT_EQ(suite->httpRequests[2].path, "/device-logs");
    EXPECT_STREQ(suite->deviceLogOf(2).device_mac, "aa:bb:cc:dd:ee:32");
    EXPECT_EQ(suite->deviceLogOf(2).crash_number, 5u);

    suite->answerSuccess(2);
    EXPECT_EQ(suite->storedCrashCount(), 0u);
    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_FALSE(suite->fdnRecordUploader->didLastCycleFail());
    EXPECT_TRUE(suite->radioIsPeerToPeer());
    EXPECT_FALSE(suite->fdnRecordUploader->hasRecordsWaiting());
}

// The kinds are independent: a match batch the server fails does not hold back crash records.
inline void fdnMatchBatchFailureDoesNotStopTheCrashUploads(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                   FdnRecordUploaderTests::relayedCrashFrame(1));

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    suite->answerHttpError(0, 500);
    EXPECT_EQ(suite->storedMatchCount(), 1u) << "a failed batch must stay stored";
    ASSERT_EQ(suite->httpRequests.size(), 2u) << "the failed batch held back the crash records";
    suite->answerSuccess(1);
    EXPECT_EQ(suite->storedCrashCount(), 0u);

    suite->fdnRecordUploader->sync();
    EXPECT_FALSE(suite->fdnRecordUploader->isUploading());
    EXPECT_TRUE(suite->fdnRecordUploader->didLastCycleFail())
    << "a cycle that left the match batch behind is a failed one";
    EXPECT_TRUE(suite->radioIsPeerToPeer());
}

// ---- The match batch ----

// The FDN holds no record key, so the tag is never checked: a record uploads
// whatever its tag, with the match fields its record bytes carry.
inline void fdnUploadsTheMatchesItsStoredRecordsCarry(FdnRecordUploaderTests* suite) {
    MatchRecordFramePacket tagged = FdnRecordStoreTests::uploadableMatchFrame(4);
    for (size_t i = 0; i < sizeof(tagged.content.tag); ++i) {
        tagged.content.tag[i] = static_cast<uint8_t>(0xA0 + i);
    }
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, tagged);
    suite->deliver(PktType::kMatchRecord, suite->otherPdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(9));

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u) << "the stored match records go up as one batch";
    EXPECT_EQ(suite->httpRequests[0].path, "/quickdraw-matches");
    EXPECT_EQ(suite->httpRequests[0].method, "POST");
    const std::vector<alleycat_device_QuickdrawMatch> uploaded =
    decodeTestRequest(suite->httpRequests[0].payload);
    ASSERT_EQ(uploaded.size(), 2u);
    EXPECT_STREQ(uploaded[0].id, "00000004-abcd-4000-8000-0123456789ab")
    << "the match fields must come from the stored record bytes, oldest record first";
    EXPECT_STREQ(uploaded[1].id, "00000009-abcd-4000-8000-0123456789ab");
}

// A torn record at the head of the match records must not fail every batch it heads.
inline void fdnMatchBatchSkipsAnUnreadableHead(FdnRecordUploaderTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(2));
    const uint8_t torn[3] = {1, 2, 3};
    suite->fdnPrefs.writeBytes(FDN_RECORD_STORE_NAMESPACE, "m_0", torn, sizeof(torn));

    ASSERT_TRUE(suite->fdnRecordUploader->uploadAllRecords());
    ASSERT_EQ(suite->httpRequests.size(), 1u);
    const std::vector<alleycat_device_QuickdrawMatch> uploaded =
    decodeTestRequest(suite->httpRequests[0].payload);
    ASSERT_EQ(uploaded.size(), 1u);
    EXPECT_STREQ(uploaded[0].id, "00000002-abcd-4000-8000-0123456789ab");
    suite->answerSuccess(0);
    EXPECT_EQ(suite->storedMatchCount(), 0u);
}
