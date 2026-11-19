#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "alleycat-server/fdn-record-uploader.hpp"
#include "crash-codec-tests.hpp"
#include "fdn-record-uploader-tests.hpp"
#include "record-relay-tests.hpp"

// PDN ring -> FDN store -> server over whole upload cycles, against stand-ins
// that answer the way alleycat-rs decides a batch.

// The body alleycat-rs sends with its batch-wide 409 for an ID stored under
// another event (src/routes/quickdraw.rs, the conflicting_id check).
inline const char CROSS_EVENT_409_BODY[] = "Match ID already belongs to another event";

// A server that refuses a batch naming a match already stored under an earlier
// event with that 409, and accepts everything else with 204.
class CrossEventServer {
public:
    /** Answers request @p request the way the server would. */
    void answer(HttpRequest& request) {
        if (request.path != MatchesApi::POST_QUICKDRAW_MATCHES_ENDPOINT) {
            request.onSuccess("");
            return;
        }
        ++matchRequests;
        const std::vector<alleycat_device_QuickdrawMatch> matches =
        decodeTestRequest(request.payload);
        for (const alleycat_device_QuickdrawMatch& match : matches) {
            if (storedUnderAnEarlierEvent.count(match.id) != 0) {
                request.onError(
                {WirelessError::INVALID_RESPONSE, "HTTP Error: 409", 409, CROSS_EVENT_409_BODY});
                return;
            }
        }
        for (const alleycat_device_QuickdrawMatch& match : matches)
            accepted.insert(match.id);
        request.onSuccess("");
    }

    std::set<std::string> storedUnderAnEarlierEvent;
    std::set<std::string> accepted;
    size_t matchRequests = 0;
};

class UploadCycleTests : public FdnRecordUploaderTests {
public:
    /** Runs one whole upload cycle against @p server, then moves to the next interval. */
    template <class Server> void runCycle(Server& server) {
        serve(server);
        clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    }

    /** One uploader tick, with every request it queues answered at once. */
    template <class Server> void serve(Server& server) {
        uploader->sync();
        while (answered < requests.size()) {
            server.answer(requests[answered++]);
            uploader->sync();
        }
    }

    /** Offers match @p number to the store as a relayed frame would; false when refused. */
    bool offerMatch(uint32_t number) {
        FdnRecordStore::MatchRing::Entry entry{};
        memcpy(entry.senderMac, pdnA, 6);
        entry.kind = static_cast<uint8_t>(RecordKind::MATCH);
        entry.recordNumber = number;
        const MatchRecord record = makeMatchRecord(number);
        memcpy(entry.content, &record, sizeof(record));
        return store->getMatchRecords().store(entry);
    }

    size_t answered = 0;
};

// #526: the second PDN's copy of a match is relayed after the server's active
// event changed; the first copy was uploaded under the earlier event, so every
// batch naming it gets the cross-event 409. The other matches in the store must
// still reach the server.
TEST_F(UploadCycleTests, oneCrossEventMatchDoesNotHoldEveryOtherMatch) {
    CrossEventServer server;
    server.storedUnderAnEarlierEvent.insert("00000000-0000-4000-8000-000000000001");
    storeMatch(1);  // the leftover from the earlier event
    storeMatch(2);
    storeMatch(3);

    for (int cycle = 0; cycle < 12; ++cycle)
        runCycle(server);  // one hour of cycles

    EXPECT_EQ(server.accepted.count("00000000-0000-4000-8000-000000000002"), 1u)
    << "match 2 never reached the server";
    EXPECT_EQ(server.accepted.count("00000000-0000-4000-8000-000000000003"), 1u)
    << "match 3 never reached the server";
    EXPECT_LE(storedMatches(), 1u) << "at most the cross-event match may stay";
}

// #526, the store side: a 409 that held the ring would pile later matches up
// behind it until the ring is full, and from then on no PDN gets a stored-ack.
TEST_F(UploadCycleTests, oneCrossEventMatchDoesNotFillTheStore) {
    CrossEventServer server;
    server.storedUnderAnEarlierEvent.insert("00000000-0000-4000-8000-000000000001");
    storeMatch(1);
    runCycle(server);

    // A normal evening: matches arrive between cycles, 4 per 5 minutes.
    uint32_t refusedByAFullStore = 0;
    for (uint32_t number = 2; number <= 60; ++number) {
        if (!offerMatch(number)) ++refusedByAFullStore;
        if (number % 4 == 0) runCycle(server);
    }

    EXPECT_EQ(refusedByAFullStore, 0u)
    << "the store refused (and so never acked) this many relayed matches";
    EXPECT_GE(server.accepted.size(), 59u) << "matches 2..60 should all be on the server";
}

// #528: the server is up and nothing is cabled. A full store must not refuse
// relayed matches until the 5-minute timer comes round; it uploads once it is
// full, at least 30 s after the last cycle ended.
TEST_F(UploadCycleTests, fullStoreUploadsWithoutWaitingForTheTimer) {
    CrossEventServer server;
    serve(server);  // the boot cycle: nothing waits yet

    // Ten minutes of play near one FDN: one match record every 5 seconds.
    uint32_t refusedByAFullStore = 0;
    for (uint32_t number = 1; number <= 120; ++number) {
        if (!offerMatch(number)) ++refusedByAFullStore;
        for (int second = 0; second < 5; ++second) {
            clock.advance(1000);
            serve(server);
        }
    }

    EXPECT_EQ(refusedByAFullStore, 0u)
    << "relayed matches refused (never acked) while the server was reachable";
    EXPECT_GE(server.accepted.size(), 88u) << "all but the last ring-full should be uploaded";
}

// #528: the early upload still rests 30 s after a cycle, so a store that fills
// at once does not keep ESP-NOW switched off.
TEST_F(UploadCycleTests, fullStoreWaitsThirtySecondsAfterTheLastCycle) {
    CrossEventServer server;
    serve(server);
    for (uint32_t number = 1; number <= FDN_MATCH_RECORD_CAPACITY; ++number) {
        ASSERT_TRUE(offerMatch(number));
    }
    clock.advance(29000);
    serve(server);
    EXPECT_EQ(server.matchRequests, 0u) << "uploaded within 30 s of the last cycle";

    clock.advance(2000);
    serve(server);
    EXPECT_EQ(server.matchRequests, 1u);
    EXPECT_EQ(storedMatches(), 0u);
}

// #528: a full store whose matches failed (5xx) waits for the interval, so a
// failing server does not keep ESP-NOW off every 45 seconds.
TEST_F(UploadCycleTests, fullStoreAfterAFailedCycleWaitsForTheInterval) {
    for (uint32_t number = 1; number <= FDN_MATCH_RECORD_CAPACITY; ++number) {
        ASSERT_TRUE(offerMatch(number));
    }
    uploader->sync();
    fail(WirelessError::SERVER_ERROR, 503);
    ASSERT_EQ(requests.size(), 1u);

    clock.advance(FDN_UPLOAD_INTERVAL_MS - 1);
    uploader->sync();
    EXPECT_EQ(requests.size(), 1u) << "a full store retried a failing server early";
    clock.advance(2);
    uploader->sync();
    EXPECT_EQ(requests.size(), 2u);
}

// #526 end to end: a real PDN relay, FDN store and uploader. The PDN carries one
// leftover match from the earlier event and plays on; every match it hands over
// must be acknowledged, and neither the FDN's ring nor the PDN's may fill.
class RelayUploadEndToEndTests : public RecordRelayTests {
public:
    /** Rebuilds the FDN side with an HTTP client so the uploader can run. */
    void SetUp() override {
        RecordRelayTests::SetUp();
        ON_CALL(httpClient, queueRequest(testing::_))
        .WillByDefault(testing::Invoke([this](HttpRequest&& request) {
            requests.push_back(std::move(request));
            return true;
        }));
        delete store;
        delete fdnWireless;
        fdnWireless = new WirelessManager(&fdnRadio, &httpClient);
        store = new FdnRecordStore(fdnWireless, &fdnStorage);
        uploader = new FdnRecordUploader(fdnWireless, store, []() { return false; });
    }

    /** The uploader goes before the store it reads. */
    void TearDown() override {
        delete uploader;
        RecordRelayTests::TearDown();
    }

    /** One whole upload cycle against @p server, then the next interval. */
    void runCycle(CrossEventServer& server) {
        uploader->sync();
        while (answered < requests.size()) {
            server.answer(requests[answered++]);
            uploader->sync();
        }
        clock.advance(FDN_UPLOAD_INTERVAL_MS + 1);
    }

    /** The match UUID text of a record whose bytes are all @p fill. */
    static std::string matchIdOf(uint8_t fill) {
        UUIDBytes id{};
        memset(id.bytes, fill, sizeof(id.bytes));
        char text[UUIDBytes::TEXT_BUFFER_SIZE];
        id.format(text);
        return text;
    }

    /** Matches the PDN holds that no FDN has acknowledged. */
    uint32_t pendingOnThePdn() {
        return pdnStorage.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0) - collectedSeq();
    }

    testing::NiceMock<MockHttpClient> httpClient;
    FdnRecordUploader* uploader = nullptr;
    std::vector<HttpRequest> requests;
    size_t answered = 0;
};

TEST_F(RelayUploadEndToEndTests, crossEventLeftoverDoesNotStopThePdnHandingMatchesOver) {
    CrossEventServer server;
    server.storedUnderAnEarlierEvent.insert(matchIdOf(0x01));
    storeMatch(0x01);  // played in the earlier event; the opponent's copy was uploaded then

    uint32_t matchesThePdnCouldNotStore = 0;
    for (uint8_t fill = 0x02; fill <= 0x60; ++fill) {
        beacon(fdnMac);
        if (fill % 4 == 0) runCycle(server);
        // MatchManager::appendRecordToStorage refuses a match while 32 are pending.
        if (pendingOnThePdn() >= MatchManager::MATCH_RING_CAPACITY) {
            ++matchesThePdnCouldNotStore;
            continue;
        }
        storeMatch(fill);
    }
    beacon(fdnMac);

    EXPECT_EQ(matchesThePdnCouldNotStore, 0u)
    << "matches the player finished and the PDN had no room to record";
    EXPECT_LT(store->getMatchRecords().getStoredCount(), FDN_MATCH_RECORD_CAPACITY)
    << "the FDN's match ring is full, so it acks nothing";
    EXPECT_EQ(pendingOnThePdn(), 0u) << "every match handed over should have been acknowledged";
}
