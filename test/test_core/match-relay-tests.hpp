#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "alleycat-server/fdn-record-store.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device-mock.hpp"
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "record-relay-tests.hpp"
#include "wireless/match-packet.hpp"
#include "wireless/record-relay-types.hpp"

/**
 * The PDN of RecordRelayTests with the "matches" namespace in real in-memory
 * storage, so the relay reads the session's own MatchManager. No record key:
 * every frame carries key ID 0.
 */
class MatchRelayTests : public RecordRelayTests {
public:
    /** Puts real storage under the "matches" namespace before the session reads it. */
    void SetUp() override {
        using testing::_;
        using testing::StrEq;
        MockStorage& storage = *pdnDevice.mockStorage;
        ON_CALL(storage, readUInt(StrEq(MATCHES_PREFS_NAMESPACE), _, _))
        .WillByDefault(
        [this](const std::string& ns, const std::string& key, uint32_t defaultValue) {
            return pdnPrefs.readUInt(ns, key, defaultValue);
        });
        ON_CALL(storage, writeUInt(StrEq(MATCHES_PREFS_NAMESPACE), _, _))
        .WillByDefault([this](const std::string& ns, const std::string& key, uint32_t value) {
            return pdnPrefs.writeUInt(ns, key, value);
        });
        ON_CALL(storage, readBytes(StrEq(MATCHES_PREFS_NAMESPACE), _, _, _))
        .WillByDefault(
        [this](const std::string& ns, const std::string& key, uint8_t* buffer, size_t maxLength) {
            return pdnPrefs.readBytes(ns, key, buffer, maxLength);
        });
        ON_CALL(storage, writeBytes(StrEq(MATCHES_PREFS_NAMESPACE), _, _, _))
        .WillByDefault(
        [this](const std::string& ns, const std::string& key, const uint8_t* data, size_t length) {
            return pdnPrefs.writeBytes(ns, key, data, length);
        });
        ON_CALL(storage, clear(StrEq(MATCHES_PREFS_NAMESPACE)))
        .WillByDefault([this](const std::string& ns) { return pdnPrefs.clear(ns); });

        RecordRelayTests::SetUp();

        // A match is stored only between two registered players, and its
        // handshake is taken only from a direct peer. The jack itself stays
        // Disconnected.
        UUIDBytes selfId{};
        UUIDBytes::parse("0b0b0b0b-0000-4000-8000-000000000001", selfId);
        Team bounty{};
        strncpy(bounty.name, "Bounty", sizeof(bounty.name) - 1);
        player->applyRegistrationProfile(selfId, "LocalBounty", bounty, {});
        pdnDevice.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK, opponentMac);
    }

    /** The session's match manager. */
    MatchManager* matchManager() const { return gameSession->getContext().matchManager; }

    /** The ID of the match stored under sequence number @p sequenceNumber by storeMatches(). */
    static std::string matchIdOf(uint32_t sequenceNumber) {
        char matchId[37];
        snprintf(matchId, sizeof(matchId), "%08u-7e1a-4000-8000-000000000000", sequenceNumber);
        return matchId;
    }

    /** Plays one match to its end as the bounty, which stores its record. */
    void finishMatch(const std::string& matchId) {
        UUIDBytes hunterId{};
        UUIDBytes::parse("0a0a0a0a-0000-4000-8000-000000000002", hunterId);
        const MatchCommand sendMatchId(opponentMac, MatchCmd::SEND_MATCH_ID, matchId.c_str(), 0,
                                       true, hunterId);
        matchManager()->listenForMatchEvents(sendMatchId);
        ASSERT_TRUE(matchManager()->getCurrentMatch().has_value())
        << "the match handshake was refused";
        matchManager()->setHunterDrawTime(180);
        matchManager()->setBountyDrawTime(120);
        matchManager()->setReceivedButtonPush();
        matchManager()->setReceivedDrawResult();
        ASSERT_TRUE(matchManager()->finalizeMatch());
        ASSERT_FALSE(matchManager()->getCurrentMatch().has_value());
    }

    /** Plays @p count matches, which take the next @p count sequence numbers. */
    void storeMatches(uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t sequenceNumber = matchManager()->getMatchSequenceNumber() + 1;
            finishMatch(matchIdOf(sequenceNumber));
            ASSERT_EQ(matchManager()->getMatchSequenceNumber(), sequenceNumber);
        }
    }

    /** collected_high_water_mark as the PDN's storage holds it. */
    uint32_t collectedMark() {
        return pdnPrefs.readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0);
    }

    /** The 112 bytes the PDN's storage holds for the match with @p sequenceNumber. */
    std::array<uint8_t, MATCH_RECORD_SIZE> storedRecord(uint32_t sequenceNumber) {
        char key[16];
        snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX,
                 static_cast<unsigned>((sequenceNumber - 1) % MATCH_RING_CAPACITY));
        std::array<uint8_t, MATCH_RECORD_SIZE> bytes{};
        EXPECT_EQ(pdnPrefs.readBytes(MATCHES_PREFS_NAMESPACE, key, bytes.data(), bytes.size()),
                  MATCH_RECORD_SIZE)
        << "no stored record for match " << sequenceNumber;
        return bytes;
    }

    /** The frame the PDN sends for the match with @p sequenceNumber while it has no record key. */
    MatchRecordFramePacket expectedFrame(uint32_t sequenceNumber) {
        MatchRecordFramePacket frame{};
        frame.kind = static_cast<uint8_t>(RecordKind::MATCH);
        frame.recordNumber = sequenceNumber;
        const std::array<uint8_t, MATCH_RECORD_SIZE> bytes = storedRecord(sequenceNumber);
        memcpy(frame.recordBytes, bytes.data(), bytes.size());
        return frame;
    }

    /** The record numbers of the match record frames sent, from index @p from on. */
    std::vector<uint32_t> sentMatchNumbers(size_t from = 0) const {
        std::vector<uint32_t> numbers;
        const std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
        framesOf<MatchRecordTraits>();
        for (size_t i = from; i < frames.size(); i++) {
            numbers.push_back(frames[i].recordNumber);
        }
        return numbers;
    }

    /** Unplugs whatever is Connected on @p jack. */
    void disconnectPeer(SerialIdentifier jack) {
        pdnDevice.fakeRemoteDeviceCoordinator.setJackStatus(jack, JackStatus::DISCONNECTED);
    }

    /** Captures the FDN's HTTP requests in fdnHttpRequests instead of refusing them. */
    void captureFdnHttpRequests() {
        ON_CALL(*fdnDevice.mockHttpClient, queueRequest(testing::_))
        .WillByDefault([this](HttpRequest&& request) {
            fdnHttpRequests.push_back(std::move(request));
            return true;
        });
    }

    /** Releases the captured requests, then the session and the store. */
    void TearDown() override {
        fdnHttpRequests.clear();
        RecordRelayTests::TearDown();
    }

    NativePrefsDriver pdnPrefs{"relay-pdn-prefs", {MATCHES_PREFS_NAMESPACE}};
    std::vector<HttpRequest> fdnHttpRequests;
};

// ---- Record frame ----

inline void matchRelaySendsAMatchInARecordFrameNumberedByItsSequenceNumber(MatchRelayTests* suite) {
    suite->storeMatches(12);
    suite->matchManager()->advanceMark(11);
    ASSERT_EQ(suite->collectedMark(), 11u);

    suite->hearBeacon(suite->fdnMac);

    const std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].toMac, relayMacArray(suite->fdnMac));
    ASSERT_EQ(frames[0].length, 135u) << "a match record goes out as one 135-byte frame";

    // Kind, number, record bytes, trailer, in that order after the sequence ID.
    const std::vector<uint8_t>& bytes = frames[0].bytes;
    EXPECT_EQ(bytes[1], 0x02);
    uint32_t recordNumber = 0;
    memcpy(&recordNumber, &bytes[2], sizeof(recordNumber));
    EXPECT_EQ(recordNumber, 12u);
    const std::array<uint8_t, MATCH_RECORD_SIZE> stored = suite->storedRecord(12);
    EXPECT_EQ(memcmp(&bytes[6], stored.data(), stored.size()), 0)
    << "the record bytes are not the 112 bytes the PDN stored for match 12";
    Match carried{};
    carried.deserialize(&bytes[6]);
    EXPECT_STREQ(carried.getMatchId(), MatchRelayTests::matchIdOf(12).c_str());
    // No record key in this build: key ID 0 and 16 zero bytes.
    for (size_t i = 6 + MATCH_RECORD_SIZE; i < bytes.size(); i++) {
        ASSERT_EQ(bytes[i], 0) << "trailer byte " << (i - 6 - MATCH_RECORD_SIZE);
    }
    EXPECT_EQ(suite->collectedMark(), 11u) << "sending a record must not retire it";
}

// ---- match-collection scenarios ----

inline void
matchRelayDeliversMatchesOneToThreeAfterABeaconWithNothingConnected(MatchRelayTests* suite) {
    suite->storeMatches(3);
    FakeRemoteDeviceCoordinator& rdc = suite->pdnDevice.fakeRemoteDeviceCoordinator;
    ASSERT_EQ(rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::DISCONNECTED);
    ASSERT_EQ(rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::DISCONNECTED);
    for (int tick = 0; tick < 5; tick++) {
        suite->pdnDevice.loop();
    }
    ASSERT_TRUE(suite->framesOf<MatchRecordTraits>().empty())
    << "a match record was sent with no FDN in reach";

    suite->hearBeacon(suite->fdnMac);
    for (uint32_t number = 1; number <= 3; number++) {
        const std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
        suite->framesOf<MatchRecordTraits>();
        ASSERT_EQ(frames.size(), number) << "match " << number << " was not sent in its turn";
        EXPECT_EQ(frames.back().recordNumber, number);
        EXPECT_EQ(frames.back().toMac, relayMacArray(suite->fdnMac));
        EXPECT_EQ(suite->collectedMark(), number - 1);
        suite->deliverAcknowledgement<MatchRecordTraits>(
        suite->fdnMac,
        RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames.back().frame));
    }

    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{1u, 2u, 3u}));
    EXPECT_EQ(suite->collectedMark(), 3u);
    EXPECT_EQ(suite->matchManager()->getCollectedHighWaterMark(), 3u);

    suite->hearBeacon(suite->fdnMac);
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 3u) << "nothing is pending any more";
}

inline void matchRelayKeepsUnacknowledgedMatchesPendingAfterAnUnplug(MatchRelayTests* suite) {
    suite->storeMatches(5);

    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    for (uint32_t number = 1; number <= 3; number++) {
        const std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
        suite->framesOf<MatchRecordTraits>();
        ASSERT_EQ(frames.size(), number);
        ASSERT_EQ(frames.back().recordNumber, number);
        suite->deliverAcknowledgement<MatchRecordTraits>(
        suite->fdnMac,
        RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames.back().frame));
    }
    // Match 4 went out on the radio, and the PDN is unplugged before the FDN
    // answers it.
    std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 4u);
    ASSERT_EQ(frames[3].recordNumber, 4u);
    suite->reportDelivered<MatchRecordTraits>(frames[3]);
    suite->disconnectPeer(SerialIdentifier::OUTPUT_JACK);

    EXPECT_EQ(suite->collectedMark(), 3u);
    for (int tick = 0; tick < 3; tick++) {
        suite->fakeClock->advance(RECORD_RELAY_BEACON_INTERVAL_MS);
        suite->pdnDevice.loop();
    }
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 4u)
    << "nothing is sent with no FDN in reach";
    EXPECT_EQ(suite->collectedMark(), 3u);

    // The next FDN contact: only matches 4 and 5 go out.
    suite->hearBeacon(suite->fdnMac);
    frames = suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_EQ(frames[4].recordNumber, 4u);
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[4].frame));
    frames = suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 6u);
    EXPECT_EQ(frames[5].recordNumber, 5u);
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[5].frame));

    EXPECT_EQ(suite->sentMatchNumbers(4), (std::vector<uint32_t>{4u, 5u}));
    EXPECT_EQ(suite->collectedMark(), 5u);
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 6u);
}

inline void
matchRelayIgnoresAnAcknowledgementFromADeviceTheRecordWasNotSentTo(MatchRelayTests* suite) {
    suite->storeMatches(5);

    suite->hearBeacon(suite->fdnMac);
    const std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].recordNumber, 1u);
    suite->reportDelivered<MatchRecordTraits>(frames[0]);

    // Right kind, number and record check; only the sender is wrong.
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->strangerMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame));
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->otherFdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame));
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->opponentMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame));

    EXPECT_EQ(suite->collectedMark(), 0u);
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 1u)
    << "a spoofed acknowledgement released match 2";

    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame));
    EXPECT_EQ(suite->collectedMark(), 1u)
    << "the same acknowledgement from the FDN the record was sent to counts";
}

// ---- Order, repeats, lengths and the upload ----

inline void matchRelayPassesOverAnUnreadableStoredMatch(MatchRelayTests* suite) {
    suite->storeMatches(2);
    const uint8_t torn[3] = {1, 2, 3};
    suite->pdnPrefs.writeBytes(MATCHES_PREFS_NAMESPACE, "match_0", torn, sizeof(torn));

    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{2u}))
    << "record 1 is unreadable and skipped";
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(suite->collectedMark(), 0u) << "passing over match 1 moved the mark";
    suite->reportDelivered<MatchRecordTraits>(frames[0]);

    // Every later trigger still offers match 2, and its acknowledgement takes
    // the mark past the unreadable record.
    suite->hearBeacon(suite->fdnMac);
    frames = suite->framesOf<MatchRecordTraits>();
    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{2u, 2u}));
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(suite->collectedMark(), 0u) << "offering match 2 again moved the mark";
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[1].frame));
    EXPECT_EQ(suite->collectedMark(), 2u);

    suite->hearBeacon(suite->fdnMac);
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 2u);
}

inline void matchRelaySendsAMatchOncePerTriggerUntilAcknowledged(MatchRelayTests* suite) {
    suite->storeMatches(1);
    const MatchRecordFramePacket expected = suite->expectedFrame(1);

    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(expected));
    EXPECT_EQ(suite->collectedMark(), 0u) << "an ack for a record not yet sent marked it collected";

    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(memcmp(frames[0].frame.recordBytes, expected.recordBytes, MATCH_RECORD_SIZE), 0);
    suite->reportDelivered<MatchRecordTraits>(frames[0]);

    // Staying Connected is not a trigger.
    for (int tick = 0; tick < 150; tick++) {
        suite->fakeClock->advance(100);
        suite->pdnDevice.loop();
    }
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 1u)
    << "one send per trigger, not per tick";
    EXPECT_EQ(suite->collectedMark(), 0u) << "a delivery report alone moved the mark";

    suite->hearBeacon(suite->fdnMac);
    frames = suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 2u) << "an unacked record is resent on the next beacon";
    EXPECT_EQ(frames[1].recordNumber, 1u);
    EXPECT_NE(frames[1].seqId, frames[0].seqId) << "a new send, not a retransmit";
    suite->reportDelivered<MatchRecordTraits>(frames[1]);

    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[1].frame));
    EXPECT_EQ(suite->collectedMark(), 1u);
    suite->hearBeacon(suite->fdnMac);
    suite->fakeClock->advance(RECORD_RELAY_BEACON_INTERVAL_MS);
    suite->pdnDevice.loop();
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 2u) << "an acked record is not resent";
}

inline void matchRelayTransfersAMatchToAConnectedFdnThatUploadsIt(MatchRelayTests* suite) {
    suite->bringUpFdn();
    suite->captureFdnHttpRequests();
    suite->storeMatches(1);
    ASSERT_EQ(suite->matchManager()->getMatchSequenceNumber(), 1u);
    ASSERT_EQ(suite->matchManager()->getCollectedHighWaterMark(), 0u);
    suite->pumpAir();

    // An acknowledgement from a stranger is ignored, before the send and after it.
    const MatchRecordFramePacket expected = suite->expectedFrame(1);
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->strangerMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(expected));
    EXPECT_EQ(suite->matchManager()->getCollectedHighWaterMark(), 0u);

    // The PDN is Connected on the FDN's jack and sends through the same relay.
    // The FDN is told nothing about the cable: its intake has no cable check.
    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    ASSERT_EQ(suite->framesOf<MatchRecordTraits>().size(), 1u);
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->strangerMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(expected));
    EXPECT_EQ(suite->matchManager()->getCollectedHighWaterMark(), 0u);
    suite->pumpAir();

    EXPECT_EQ(suite->matchManager()->getCollectedHighWaterMark(), 1u);
    EXPECT_EQ(suite->collectedMark(), 1u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 1u);

    std::vector<bool> outcomes;
    auto onComplete = [&outcomes](bool uploaded) { outcomes.push_back(uploaded); };
    ASSERT_TRUE(suite->fdnRecordStore->uploadPendingBatch(onComplete));
    ASSERT_EQ(suite->fdnHttpRequests.size(), 1u);
    EXPECT_EQ(suite->fdnHttpRequests[0].path, "/quickdraw-matches");
    suite->fdnHttpRequests[0].onError(WirelessErrorInfo{WirelessError::SERVER_ERROR, "HTTP 500"});
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u)
    << "5xx error must retain stored batch";

    suite->fdnHttpRequests.clear();
    ASSERT_TRUE(suite->fdnRecordStore->uploadPendingBatch(onComplete));
    ASSERT_EQ(suite->fdnHttpRequests.size(), 1u);
    suite->fdnHttpRequests[0].onSuccess("");
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u)
    << "HTTP 204 must retire uploaded batch";
    EXPECT_EQ(outcomes, (std::vector<bool>{false, true}));
    uint8_t retired[256] = {};
    EXPECT_EQ(
    suite->fdnPrefs.readBytes(FDN_RECORD_STORE_NAMESPACE, "m_0", retired, sizeof(retired)), 0u)
    << "a retired record still holds its NVS entries";
}

inline void matchRelayDropsAnAcknowledgementOfTheWrongLength(MatchRelayTests* suite) {
    suite->storeMatches(1);
    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    const std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    suite->reportDelivered<MatchRecordTraits>(frames[0]);

    // The right acknowledgement, one byte too long and one byte too short.
    RecordAckPacket ack = RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame);
    ack.seqId = 200;
    uint8_t longAck[sizeof(RecordAckPacket) + 1] = {};
    memcpy(longAck, &ack, sizeof(ack));
    suite->pdnDevice.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kMatchRecordAck,
                                                          longAck, sizeof(longAck));
    ack.seqId = 201;
    suite->pdnDevice.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kMatchRecordAck,
                                                          reinterpret_cast<const uint8_t*>(&ack),
                                                          sizeof(ack) - 1);
    EXPECT_EQ(suite->matchManager()->getCollectedHighWaterMark(), 0u);

    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame));
    EXPECT_EQ(suite->matchManager()->getCollectedHighWaterMark(), 1u);
}

// ---- Relay target ----

inline void matchRelayDeliversToAnotherFdnWhileConnectedToOne(MatchRelayTests* suite) {
    suite->storeMatches(1);

    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].toMac, relayMacArray(suite->fdnMac));
    // Delivered and never acknowledged, as when the Connected FDN is full.
    suite->reportDelivered<MatchRecordTraits>(frames[0]);

    suite->hearBeacon(suite->otherFdnMac);
    frames = suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 1u);
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->otherFdnMac));
    suite->reportDelivered<MatchRecordTraits>(frames[1]);

    // Both acknowledgements carry the same kind, number and record check, so
    // only the sender tells the two apart.
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame));
    EXPECT_EQ(suite->collectedMark(), 0u) << "the earlier FDN's acknowledgement moved the mark";
    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->otherFdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[1].frame));
    EXPECT_EQ(suite->collectedMark(), 1u);
}

// ---- Play gate ----

inline void matchRelaySendsNoMatchRecordDuringAMatch(MatchRelayTests* suite) {
    suite->storeMatches(2);

    suite->startMatch();
    suite->pdnDevice.loop();
    suite->hearBeacon(suite->fdnMac);
    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    for (int tick = 0; tick < 5; tick++) {
        suite->fakeClock->advance(100);
        suite->pdnDevice.loop();
    }
    EXPECT_TRUE(suite->framesOf<MatchRecordTraits>().empty())
    << "a match record was sent during a match";
    EXPECT_TRUE(suite->framesOf<CrashRecordTraits>().empty());

    suite->endMatch();
    suite->pdnDevice.loop();
    EXPECT_TRUE(suite->framesOf<MatchRecordTraits>().empty())
    << "the end of a match is not a trigger";

    suite->hearBeacon(suite->fdnMac);
    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{1u}));
    EXPECT_EQ(suite->collectedMark(), 0u);
}

inline void
matchRelayStopsASendWhenAMatchStartsAndStillTakesItsAcknowledgement(MatchRelayTests* suite) {
    suite->storeMatches(2);

    suite->hearBeacon(suite->fdnMac);
    std::vector<SentRecordFrame<MatchRecordFramePacket>> frames =
    suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);

    // No tick in between: the send is cancelled as the match starts, ahead of
    // the transport's next retry pass.
    suite->startMatch();
    for (int tick = 0; tick < 8; tick++) {
        suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
        suite->pdnDevice.loop();
    }
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 1u)
    << "the frame was retried during the match";
    EXPECT_EQ(suite->collectedMark(), 0u);

    suite->deliverAcknowledgement<MatchRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<MatchRecordTraits>(frames[0].frame));
    EXPECT_EQ(suite->collectedMark(), 1u) << "an acknowledgement mid-match still moves the mark";
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 1u)
    << "the next record was sent during the match";

    suite->endMatch();
    suite->pdnDevice.loop();
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 1u)
    << "the next record waits for a trigger";

    suite->hearBeacon(suite->fdnMac);
    frames = suite->framesOf<MatchRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 2u);
}

// ---- One kind does not hold back the other, against a real FDN record store ----

inline void matchRelayIsNotHeldBackByACrashRecordAFullFdnWillNotTake(MatchRelayTests* suite) {
    suite->bringUpFdn();
    uint8_t otherPdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x09};
    for (uint32_t number = 1; number <= FDN_CRASH_RECORD_CAPACITY; number++) {
        CrashRecordFramePacket frame{};
        frame.seqId = static_cast<uint8_t>(number);
        frame.kind = static_cast<uint8_t>(RecordKind::CRASH);
        frame.recordNumber = number;
        memset(frame.recordBytes, 0xE0, sizeof(frame.recordBytes));
        suite->fdnDevice.mockPeerComms->triggerPacketCallback(
        otherPdnMac, PktType::kCrashRecord, reinterpret_cast<const uint8_t*>(&frame),
        sizeof(frame));
    }
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), FDN_CRASH_RECORD_CAPACITY);
    suite->crashSource.newestNumber = 1;
    suite->storeMatches(2);
    suite->pumpAir();

    suite->hearBeacon(suite->fdnMac);
    suite->pumpAir();

    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{1u, 2u}));
    EXPECT_EQ(suite->collectedMark(), 2u) << "the stuck crash record held back the match records";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);
    EXPECT_EQ(suite->crashSource.mark, 0u) << "a record the FDN could not store was retired";
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);

    // The crash record is offered again; no match record is left to send.
    suite->hearBeacon(suite->fdnMac);
    suite->pumpAir();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);
    EXPECT_EQ(suite->crashSource.mark, 0u);
    EXPECT_EQ(suite->framesOf<MatchRecordTraits>().size(), 2u);
}

inline void matchRelayHeldByAFullFdnDoesNotHoldBackCrashRecords(MatchRelayTests* suite) {
    suite->bringUpFdn();
    suite->captureFdnHttpRequests();
    uint8_t otherPdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x09};
    for (uint32_t number = 1; number <= FDN_MATCH_RECORD_CAPACITY; number++) {
        char matchId[37];
        snprintf(matchId, sizeof(matchId), "%08u-f111-4000-8000-000000000000", number);
        const Match match(matchId);
        MatchRecordFramePacket frame{};
        frame.seqId = static_cast<uint8_t>(number);
        frame.kind = static_cast<uint8_t>(RecordKind::MATCH);
        frame.recordNumber = number;
        match.serialize(frame.recordBytes);
        suite->fdnDevice.mockPeerComms->triggerPacketCallback(
        otherPdnMac, PktType::kMatchRecord, reinterpret_cast<const uint8_t*>(&frame),
        sizeof(frame));
    }
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), FDN_MATCH_RECORD_CAPACITY);
    suite->crashSource.newestNumber = 2;
    suite->storeMatches(1);
    suite->pumpAir();

    suite->hearBeacon(suite->fdnMac);
    suite->pumpAir();

    EXPECT_EQ(suite->crashSource.mark, 2u) << "the stuck match record held back the crash records";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u);
    EXPECT_EQ(suite->collectedMark(), 0u) << "a match the FDN could not store was retired";
    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{1u}));
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), FDN_MATCH_RECORD_CAPACITY);

    // Still in the PDN's ring, and offered again on the next beacon.
    suite->hearBeacon(suite->fdnMac);
    suite->pumpAir();
    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{1u, 1u}));
    EXPECT_EQ(suite->collectedMark(), 0u);

    // Once the FDN has drained its spool to the server there is room again.
    ASSERT_TRUE(suite->fdnRecordStore->uploadPendingBatch(nullptr));
    ASSERT_EQ(suite->fdnHttpRequests.size(), 1u);
    suite->fdnHttpRequests[0].onSuccess("");
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
    suite->fdnDevice.wirelessManager->enablePeerCommsMode();

    suite->hearBeacon(suite->fdnMac);
    suite->pumpAir();
    EXPECT_EQ(suite->sentMatchNumbers(), (std::vector<uint32_t>{1u, 1u, 1u}));
    EXPECT_EQ(suite->collectedMark(), 1u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
}
