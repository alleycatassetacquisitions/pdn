#pragma once

// #518: the time a match is recorded with. A PDN learns the time over a cable
// only (from an FDN, or from a peer at the match handshake), re-dates the
// matches of this power-on when it does, and never hands on a match without a
// real time. An FDN asks the server for the time at boot and holds match
// uploads until its clock is set.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstring>
#include <vector>

#include "alleycat-server/fdn-record-store.hpp"
#include "alleycat-server/fdn-record-uploader.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "fdn-record-uploader-tests.hpp"
#include "match-manager-tests.hpp"
#include "quickdraw-match-codec-tests.hpp"
#include "utils/http-date.hpp"

inline constexpr int64_t SYNCED_UTC_MS = 1792359000000LL;

// A PDN's match ring beside an FDN's record store and uploader; the relay hop
// between them is a byte copy (RecordRelayTests cover it).
class MatchTimeTests : public MatchManagerTestSuite {
public:
    /** Adds an FDN whose requests are captured in fdnRequests. */
    void SetUp() override {
        MatchManagerTestSuite::SetUp();
        ON_CALL(fdnHttpClient, queueRequest(testing::_))
        .WillByDefault(testing::Invoke([this](HttpRequest&& request) {
            fdnRequests.push_back(std::move(request));
            return true;
        }));
        fdnWireless = new WirelessManager(&fdnPeerComms, &fdnHttpClient);
        fdnStore = new FdnRecordStore(fdnWireless, &fdnStorage);
        fdnUploader = new FdnRecordUploader(fdnWireless, fdnStore, []() { return false; });
    }

    /** Deletes the FDN, then the PDN fixture. */
    void TearDown() override {
        delete fdnUploader;
        delete fdnStore;
        delete fdnWireless;
        MatchManagerTestSuite::TearDown();
    }

    /** Moves the PDN's lowest pending match into the FDN's store, as a frame and its ack do. */
    bool relayLowestPendingMatch() {
        FdnRecordStore::MatchRing::Entry entry{};
        uint32_t recordNumber = 0;
        if (!matchManager->readLowestPending(recordNumber, entry.content)) return false;
        memcpy(entry.senderMac, pdnMac, sizeof(entry.senderMac));
        entry.kind = static_cast<uint8_t>(RecordKind::MATCH);
        entry.recordNumber = recordNumber;
        return fdnStore->getMatchRecords().store(entry) && matchManager->advanceMark(recordNumber);
    }

    /** A PDN that no FDN has set since power-on reads its uptime as the time. */
    void unsetClockAt(unsigned long uptimeMs) {
        fakeClock->setTime(uptimeMs);
        fakeClock->setUnixTimeMs(uptimeMs);
    }

    const uint8_t pdnMac[6] = {0x0A, 0x01, 0x02, 0x03, 0x04, 0x05};
    testing::NiceMock<MockPeerComms> fdnPeerComms;
    testing::NiceMock<MockHttpClient> fdnHttpClient;
    NativePrefsDriver fdnStorage{"match-time-fdn-prefs", {FDN_RECORD_STORE_NAMESPACE}};
    WirelessManager* fdnWireless = nullptr;
    FdnRecordStore* fdnStore = nullptr;
    FdnRecordUploader* fdnUploader = nullptr;
    std::vector<HttpRequest> fdnRequests;
};

// The server scores a match only inside a scoring period and keeps the first
// report of a match id, so a time the PDN never knew must not go up as 1970.
// The match waits, marked, until the PDN learns the time, and then carries the
// time it was really played.
TEST_F(MatchTimeTests, matchPlayedBeforeTheClockIsSetWaitsAndIsRedated) {
    unsetClockAt(95000);
    ASSERT_TRUE(playHunterWin());
    EXPECT_EQ(storedMatch(1).getOccurredAtUnixMs(), 0) << "an unknown time is stored as 0";
    EXPECT_FALSE(relayLowestPendingMatch()) << "a match without a real time was handed on";

    fakeClock->setTime(100000);
    fakeClock->setUnixTimeMs(SYNCED_UTC_MS);
    ASSERT_TRUE(relayLowestPendingMatch());
    fdnUploader->sync();

    ASSERT_EQ(fdnRequests.size(), 1u);
    const std::vector<alleycat_device_QuickdrawMatch> matches =
    decodeTestRequest(fdnRequests[0].payload);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].occurred_at_unix_ms, SYNCED_UTC_MS - 5000)
    << "the match was not re-dated to the moment it was played";
}

// A match from an earlier power-on cannot be re-dated: its uptime means nothing
// now. It stays on the PDN, marked, and later matches go past it.
TEST_F(MatchTimeTests, matchFromAnEarlierPowerOnIsNeverHandedOn) {
    unsetClockAt(95000);
    ASSERT_TRUE(playHunterWin());

    delete matchManager;  // power cycle: the ring stays, the uptime is gone
    matchManager = new MatchManager(device.wirelessManager);
    matchManager->initialize(player, &storage);
    matchManager->setRemoteDeviceCoordinator(&fakeRdc);
    unsetClockAt(2000);
    fakeClock->setUnixTimeMs(SYNCED_UTC_MS);
    ASSERT_TRUE(playHunterWin());

    uint32_t recordNumber = 0;
    uint8_t bytes[MatchRecordTraits::RECORD_SIZE];
    ASSERT_TRUE(matchManager->readLowestPending(recordNumber, bytes));
    EXPECT_EQ(recordNumber, 2u);
    ASSERT_TRUE(matchManager->advanceMark(recordNumber));
    EXPECT_FALSE(matchManager->readLowestPending(recordNumber, bytes));
    EXPECT_EQ(storedMatch(1).getOccurredAtUnixMs(), 0);
}

// A ring full of matches from an earlier power-on with no time could never
// drain; the next match must still be stored and handed on.
TEST_F(MatchTimeTests, ringFullOfUndatableMatchesStillTakesTheNextOne) {
    for (uint32_t played = 0; played < MatchManager::MATCH_RING_CAPACITY; played++) {
        unsetClockAt(95000 + played * 1000);
        ASSERT_TRUE(playHunterWin());
    }

    delete matchManager;  // power cycle: the ring stays, the uptime is gone
    matchManager = new MatchManager(device.wirelessManager);
    matchManager->initialize(player, &storage);
    matchManager->setRemoteDeviceCoordinator(&fakeRdc);
    unsetClockAt(2000);
    fakeClock->setUnixTimeMs(SYNCED_UTC_MS);
    ASSERT_TRUE(playHunterWin());

    uint32_t recordNumber = 0;
    uint8_t bytes[MatchRecordTraits::RECORD_SIZE];
    ASSERT_TRUE(matchManager->readLowestPending(recordNumber, bytes))
    << "the match after a ring of undatable ones was not stored";
    EXPECT_EQ(recordNumber, MatchManager::MATCH_RING_CAPACITY + 1);
}

// The handshake is cabled, so it carries time the way an FDN's context does: a
// PDN without the time takes its peer's; one with the time keeps its own.
TEST_F(MatchTimeTests, bountyWithoutTheTimeTakesTheHuntersAtTheHandshake) {
    unsetClockAt(5000);
    player->setIsHunter(false);
    uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand handshake(hunterMac, MatchCmd::SEND_MATCH_ID, "match-time-1", 0, true);
    handshake.utcUnixMs = SYNCED_UTC_MS;
    matchManager->listenForMatchEvents(handshake);
    EXPECT_EQ(fakeClock->unixMilliseconds(), SYNCED_UTC_MS);
}

TEST_F(MatchTimeTests, hunterWithoutTheTimeTakesTheBountysFromTheAck) {
    unsetClockAt(5000);
    setupMatchAsHunter();
    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->utcUnixMs, 0) << "a PDN without the time sent one";

    uint8_t bountyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand ack(bountyMac, MatchCmd::MATCH_ID_ACK,
                     matchManager->getCurrentMatch()->getMatchId(), 0, false);
    ack.utcUnixMs = SYNCED_UTC_MS;
    matchManager->listenForMatchEvents(ack);
    EXPECT_EQ(fakeClock->unixMilliseconds(), SYNCED_UTC_MS);
}

TEST_F(MatchTimeTests, aPdnWithTheTimeSendsItAndKeepsItsOwn) {
    fakeClock->setUnixTimeMs(SYNCED_UTC_MS);
    player->setIsHunter(false);
    uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand handshake(hunterMac, MatchCmd::SEND_MATCH_ID, "match-time-2", 0, true);
    handshake.utcUnixMs = SYNCED_UTC_MS + 60000;
    matchManager->listenForMatchEvents(handshake);
    EXPECT_EQ(fakeClock->unixMilliseconds(), SYNCED_UTC_MS) << "a peer overwrote a set clock";
    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->utcUnixMs, SYNCED_UTC_MS) << "the ack did not carry the time";
}

// An FDN learns the time from an answer's Date header. Without the time it asks
// at boot even with nothing stored, and holds matches (not crash records) until
// its clock is set.
TEST_F(FdnRecordUploaderTests, fdnWithoutTheTimeAsksAtBootEvenWithNothingStored) {
    clock.setUnixTimeMs(0);
    uploader->sync();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].path, HealthApi::GET_HEALTH_CHECK_ENDPOINT);
    EXPECT_EQ(requests[0].method, "GET");
}

TEST_F(FdnRecordUploaderTests, fdnHoldsMatchesUntilItsClockIsSet) {
    clock.setUnixTimeMs(0);
    storeMatch(1);
    storeCrash(1);
    uploader->sync();
    succeed();  // the time check answered, but no Date header set the clock
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[1].path, CrashApi::POST_DEVICE_LOGS_ENDPOINT);
    succeed();
    EXPECT_EQ(storedMatches(), 1u);

    clock.setUnixTimeMs(SYNCED_UTC_MS);
    clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    uploader->sync();
    ASSERT_EQ(requests.size(), 3u);
    EXPECT_EQ(requests[2].path, MatchesApi::POST_QUICKDRAW_MATCHES_ENDPOINT);
}

// A failed time check (no AP, no answer) still lets the crash records go.
TEST_F(FdnRecordUploaderTests, failedTimeCheckStillSendsCrashRecords) {
    clock.setUnixTimeMs(0);
    storeMatch(1);
    storeCrash(1);
    uploader->sync();
    fail(WirelessError::TIMEOUT, 0);
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[1].path, CrashApi::POST_DEVICE_LOGS_ENDPOINT);
    EXPECT_EQ(storedMatches(), 1u);
}
