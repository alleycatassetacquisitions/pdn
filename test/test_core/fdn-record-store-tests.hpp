#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "alleycat-server/fdn-record-store.hpp"
#include "device-mock.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "match-manager-tests.hpp"
#include "utility-tests.hpp"
#include "wireless/remote-player-manager.hpp"

// Native prefs whose byte and uint writes a test can make fail.
class RefusingPrefsDriver : public NativePrefsDriver {
public:
    using NativePrefsDriver::NativePrefsDriver;

    /** Fails while refuseBytes is set. */
    size_t writeBytes(const std::string& ns, const std::string& key, const uint8_t* data,
                      size_t length) override {
        if (refuseBytes) return 0;
        return NativePrefsDriver::writeBytes(ns, key, data, length);
    }

    /** Fails while refuseUInt is set. */
    size_t writeUInt(const std::string& ns, const std::string& key, uint32_t value) override {
        if (refuseUInt) return 0;
        return NativePrefsDriver::writeUInt(ns, key, value);
    }

    bool refuseBytes = false;
    bool refuseUInt = false;
};

class FdnRecordStoreTests : public testing::Test {
public:
    /** Builds the store on a mocked radio that captures handlers and acks. */
    void SetUp() override {
        SimpleTimer::setPlatformClock(&clock);
        ON_CALL(peerComms, getPeerCommsState())
        .WillByDefault(testing::Return(PeerCommsState::ENABLED));
        ON_CALL(peerComms, setPacketHandler(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Invoke([this](PktType type, PeerCommsInterface::PacketCallback cb,
                                              void* ctx) { handlers[type] = {cb, ctx}; }));
        ON_CALL(peerComms, sendData(testing::_, PktType::kRecordAck, testing::_, testing::_))
        .WillByDefault(
        testing::Invoke([this](const uint8_t* dst, PktType, const uint8_t* data, const size_t len) {
            RecordAckPacket ack{};
            EXPECT_EQ(len, sizeof(ack));
            memcpy(&ack, data, sizeof(ack));
            acks.push_back(ack);
            memcpy(lastAckDestination, dst, 6);
            return 0;
        }));
        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        store = new FdnRecordStore(wirelessManager, &storage);
    }

    /** Frees the store before the radio it registered with. */
    void TearDown() override {
        delete store;
        delete wirelessManager;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /** Rebuilds the store on the same prefs, as a reboot would. */
    void restart() {
        delete store;
        store = new FdnRecordStore(wirelessManager, &storage);
    }

    /** Plays raw bytes at the handler for @p type as the radio would. */
    void deliverBytes(PktType type, const uint8_t* fromMac, const void* data, size_t len) {
        ASSERT_EQ(handlers.count(type), 1u);
        handlers[type].first(fromMac, static_cast<const uint8_t*>(data), len,
                             handlers[type].second);
    }

    /** A crash frame whose content bytes are all @p fill. */
    CrashRecordFramePacket crashFrame(uint32_t number, uint8_t fill) {
        CrashRecordFramePacket frame{};
        frame.seqId = nextSeqId++;
        frame.kind = static_cast<uint8_t>(RecordKind::CRASH);
        frame.recordNumber = number;
        memset(frame.content, fill, sizeof(frame.content));
        return frame;
    }

    /** A match frame whose content bytes are all @p fill. */
    MatchRecordFramePacket matchFrame(uint32_t number, uint8_t fill) {
        MatchRecordFramePacket frame{};
        frame.seqId = nextSeqId++;
        frame.kind = static_cast<uint8_t>(RecordKind::MATCH);
        frame.recordNumber = number;
        memset(frame.content, fill, sizeof(frame.content));
        return frame;
    }

    /** Delivers a crash frame on its own packet type. */
    void deliver(const uint8_t* fromMac, const CrashRecordFramePacket& frame) {
        deliverBytes(PktType::kCrashRecord, fromMac, &frame, sizeof(frame));
    }

    /** Delivers a match frame on its own packet type. */
    void deliver(const uint8_t* fromMac, const MatchRecordFramePacket& frame) {
        deliverBytes(PktType::kMatchRecord, fromMac, &frame, sizeof(frame));
    }

    const uint8_t pdnA[6] = {0x0A, 0x01, 0x02, 0x03, 0x04, 0x05};
    const uint8_t pdnB[6] = {0x0B, 0x01, 0x02, 0x03, 0x04, 0x05};
    FakePlatformClock clock;
    testing::NiceMock<MockPeerComms> peerComms;
    testing::NiceMock<MockHttpClient> httpClient;
    RefusingPrefsDriver storage{"fdn", {FDN_RECORD_STORE_NAMESPACE, LEGACY_MATCHES_NAMESPACE}};
    WirelessManager* wirelessManager = nullptr;
    FdnRecordStore* store = nullptr;
    std::map<PktType, std::pair<PeerCommsInterface::PacketCallback, void*>> handlers;
    std::vector<RecordAckPacket> acks;
    uint8_t lastAckDestination[6] = {};
    uint8_t nextSeqId = 1;
};

TEST_F(FdnRecordStoreTests, storesMatchThenAcksItsSender) {
    const MatchRecordFramePacket frame = matchFrame(7, 0x5A);
    deliver(pdnA, frame);

    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0].kind, static_cast<uint8_t>(RecordKind::MATCH));
    EXPECT_EQ(acks[0].recordNumber, 7u);
    EXPECT_EQ(acks[0].contentCrc, crc16(frame.content, MATCH_BINARY_SIZE));
    EXPECT_EQ(memcmp(lastAckDestination, pdnA, 6), 0);

    FdnRecordStore::MatchRing::Entry entries[2];
    ASSERT_EQ(store->getMatchRecords().peekOldest(entries, 2), 1u);
    EXPECT_EQ(memcmp(entries[0].senderMac, pdnA, 6), 0);
    EXPECT_EQ(entries[0].recordNumber, 7u);
    EXPECT_EQ(memcmp(entries[0].content, frame.content, MATCH_BINARY_SIZE), 0);
}

TEST_F(FdnRecordStoreTests, storesCrashThenAcksWithItsKind) {
    const CrashRecordFramePacket frame = crashFrame(3, 0x11);
    deliver(pdnA, frame);

    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0].kind, static_cast<uint8_t>(RecordKind::CRASH));
    EXPECT_EQ(acks[0].contentCrc, crc16(frame.content, CRASH_RECORD_SIZE));
    EXPECT_EQ(store->getCrashRecords().getStoredCount(), 1u);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 0u);
}

TEST_F(FdnRecordStoreTests, unackedAckIsResentFromSync) {
    deliver(pdnA, matchFrame(1, 0x01));
    ASSERT_EQ(acks.size(), 1u);

    clock.advance(Resender::INITIAL_TIMEOUT_MS + 1);
    store->sync();

    ASSERT_GE(acks.size(), 2u);
    EXPECT_EQ(acks.back().recordNumber, 1u);
}

TEST_F(FdnRecordStoreTests, crashAndMatchAcksToOnePdnBothStayPending) {
    deliver(pdnA, crashFrame(1, 0x01));
    deliver(pdnA, matchFrame(1, 0x02));
    acks.clear();

    clock.advance(Resender::INITIAL_TIMEOUT_MS + 1);
    store->sync();

    bool resentCrash = false;
    bool resentMatch = false;
    for (const RecordAckPacket& ack : acks) {
        resentCrash |= ack.kind == static_cast<uint8_t>(RecordKind::CRASH);
        resentMatch |= ack.kind == static_cast<uint8_t>(RecordKind::MATCH);
    }
    EXPECT_TRUE(resentCrash);
    EXPECT_TRUE(resentMatch);
}

TEST_F(FdnRecordStoreTests, fullRingRefusesWithoutAckAndNeverOverwrites) {
    for (uint32_t number = 0; number < FDN_CRASH_RECORD_CAPACITY; ++number) {
        deliver(pdnA, crashFrame(number, static_cast<uint8_t>(number)));
    }
    ASSERT_EQ(acks.size(), FDN_CRASH_RECORD_CAPACITY);

    deliver(pdnA, crashFrame(100, 0xEE));

    EXPECT_EQ(acks.size(), FDN_CRASH_RECORD_CAPACITY);
    EXPECT_EQ(store->getCrashRecords().getStoredCount(), FDN_CRASH_RECORD_CAPACITY);
    FdnRecordStore::CrashRing::Entry oldest[1];
    ASSERT_EQ(store->getCrashRecords().peekOldest(oldest, 1), 1u);
    EXPECT_EQ(oldest[0].recordNumber, 0u);
}

TEST_F(FdnRecordStoreTests, retiringFreesRoomForTheNextRecord) {
    for (uint32_t number = 0; number < FDN_CRASH_RECORD_CAPACITY; ++number) {
        deliver(pdnA, crashFrame(number, static_cast<uint8_t>(number)));
    }
    store->getCrashRecords().retireOldest(2);
    acks.clear();

    deliver(pdnA, crashFrame(100, 0xEE));

    ASSERT_EQ(acks.size(), 1u);
    FdnRecordStore::CrashRing::Entry entries[FDN_CRASH_RECORD_CAPACITY];
    ASSERT_EQ(store->getCrashRecords().peekOldest(entries, FDN_CRASH_RECORD_CAPACITY),
              FDN_CRASH_RECORD_CAPACITY - 1);
    EXPECT_EQ(entries[0].recordNumber, 2u);
    EXPECT_EQ(entries[FDN_CRASH_RECORD_CAPACITY - 2].recordNumber, 100u);
}

TEST_F(FdnRecordStoreTests, duplicateIsAckedAgainAndStoredOnce) {
    MatchRecordFramePacket frame = matchFrame(9, 0x33);
    deliver(pdnA, frame);
    // A resend after a missed ack carries a fresh seqId, so the channel passes it.
    frame.seqId = nextSeqId++;
    deliver(pdnA, frame);

    ASSERT_EQ(acks.size(), 2u);
    EXPECT_EQ(acks[1].recordNumber, 9u);
    EXPECT_EQ(acks[1].contentCrc, acks[0].contentCrc);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 1u);
}

TEST_F(FdnRecordStoreTests, duplicateIsAckedEvenWhenFull) {
    for (uint32_t number = 0; number < FDN_CRASH_RECORD_CAPACITY; ++number) {
        deliver(pdnA, crashFrame(number, static_cast<uint8_t>(number)));
    }
    deliver(pdnA, crashFrame(4, 4));

    EXPECT_EQ(acks.size(), FDN_CRASH_RECORD_CAPACITY + 1);
    EXPECT_EQ(acks.back().recordNumber, 4u);
}

TEST_F(FdnRecordStoreTests, sameNumberWithDifferentBytesIsStoredSeparately) {
    deliver(pdnA, matchFrame(9, 0x33));
    deliver(pdnA, matchFrame(9, 0x44));

    ASSERT_EQ(acks.size(), 2u);
    EXPECT_NE(acks[0].contentCrc, acks[1].contentCrc);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 2u);
}

TEST_F(FdnRecordStoreTests, sameRecordFromTwoPdnsIsStoredForEach) {
    deliver(pdnA, matchFrame(9, 0x33));
    deliver(pdnB, matchFrame(9, 0x33));

    EXPECT_EQ(acks.size(), 2u);
    EXPECT_EQ(memcmp(lastAckDestination, pdnB, 6), 0);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 2u);
}

TEST_F(FdnRecordStoreTests, wrongKindIsDroppedWithoutAck) {
    MatchRecordFramePacket frame = matchFrame(1, 0x01);
    frame.kind = 0x03;
    deliver(pdnA, frame);
    CrashRecordFramePacket crash = crashFrame(1, 0x01);
    crash.kind = static_cast<uint8_t>(RecordKind::MATCH);
    deliver(pdnA, crash);

    EXPECT_TRUE(acks.empty());
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 0u);
    EXPECT_EQ(store->getCrashRecords().getStoredCount(), 0u);
}

TEST_F(FdnRecordStoreTests, wrongLengthIsDroppedWithoutAck) {
    const MatchRecordFramePacket frame = matchFrame(1, 0x01);
    deliverBytes(PktType::kMatchRecord, pdnA, &frame, sizeof(frame) - 1);

    EXPECT_TRUE(acks.empty());
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 0u);
}

TEST_F(FdnRecordStoreTests, refusedBlobWriteIsNotAcked) {
    storage.refuseBytes = true;
    deliver(pdnA, matchFrame(1, 0x01));

    EXPECT_TRUE(acks.empty());
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 0u);
}

TEST_F(FdnRecordStoreTests, refusedTailCommitIsNotAckedAndLeavesNothingAfterRestart) {
    storage.refuseUInt = true;
    deliver(pdnA, matchFrame(1, 0x01));
    storage.refuseUInt = false;

    EXPECT_TRUE(acks.empty());
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 0u);
    restart();
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 0u);
}

TEST_F(FdnRecordStoreTests, storeSurvivesRestartAndStillDedupes) {
    const MatchRecordFramePacket frame = matchFrame(5, 0x55);
    deliver(pdnA, frame);
    deliver(pdnA, matchFrame(6, 0x66));
    store->getMatchRecords().retireOldest(1);

    restart();

    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 1u);
    FdnRecordStore::MatchRing::Entry entries[1];
    ASSERT_EQ(store->getMatchRecords().peekOldest(entries, 1), 1u);
    EXPECT_EQ(entries[0].recordNumber, 6u);

    acks.clear();
    deliver(pdnA, matchFrame(6, 0x66));
    EXPECT_EQ(acks.size(), 1u);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 1u);
}

TEST_F(FdnRecordStoreTests, unreadableHeadIsRetired) {
    deliver(pdnA, matchFrame(1, 0x01));
    deliver(pdnA, matchFrame(2, 0x02));
    const uint8_t junk[3] = {1, 2, 3};
    storage.writeBytes(FDN_RECORD_STORE_NAMESPACE, "m_0", junk, sizeof(junk));

    FdnRecordStore::MatchRing::Entry entries[2];
    ASSERT_EQ(store->getMatchRecords().peekOldest(entries, 2), 1u);
    EXPECT_EQ(entries[0].recordNumber, 2u);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 1u);
}

TEST_F(FdnRecordStoreTests, unreadableEntryBehindTheHeadStopsThePeek) {
    deliver(pdnA, matchFrame(1, 0x01));
    deliver(pdnA, matchFrame(2, 0x02));
    const uint8_t junk[3] = {1, 2, 3};
    storage.writeBytes(FDN_RECORD_STORE_NAMESPACE, "m_1", junk, sizeof(junk));

    FdnRecordStore::MatchRing::Entry entries[2];
    ASSERT_EQ(store->getMatchRecords().peekOldest(entries, 2), 1u);
    EXPECT_EQ(entries[0].recordNumber, 1u);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 2u);
}

// A slot cut short keeps a valid kind byte; read whole, its missing tail would
// be taken from whatever the reader's buffer held and uploaded.
TEST_F(FdnRecordStoreTests, shortSlotIsRetiredNotOffered) {
    deliver(pdnA, matchFrame(1, 0x01));
    deliver(pdnA, matchFrame(2, 0x02));
    FdnRecordStore::MatchRing::Entry cut{};
    memcpy(cut.senderMac, pdnA, 6);
    cut.kind = static_cast<uint8_t>(RecordKind::MATCH);
    cut.recordNumber = 1;
    storage.writeBytes(FDN_RECORD_STORE_NAMESPACE, "m_0", reinterpret_cast<const uint8_t*>(&cut),
                       sizeof(cut) - 1);

    FdnRecordStore::MatchRing::Entry entries[2];
    ASSERT_EQ(store->getMatchRecords().peekOldest(entries, 2), 1u);
    EXPECT_EQ(entries[0].recordNumber, 2u) << "the cut slot was offered for upload";
}

TEST_F(FdnRecordStoreTests, slotOfAnotherKindIsRetiredNotOffered) {
    deliver(pdnA, matchFrame(1, 0x01));
    deliver(pdnA, matchFrame(2, 0x02));
    FdnRecordStore::MatchRing::Entry crashKind{};
    memcpy(crashKind.senderMac, pdnA, 6);
    crashKind.kind = static_cast<uint8_t>(RecordKind::CRASH);
    crashKind.recordNumber = 1;
    storage.writeBytes(FDN_RECORD_STORE_NAMESPACE, "m_0",
                       reinterpret_cast<const uint8_t*>(&crashKind), sizeof(crashKind));

    FdnRecordStore::MatchRing::Entry entries[2];
    ASSERT_EQ(store->getMatchRecords().peekOldest(entries, 2), 1u);
    EXPECT_EQ(entries[0].recordNumber, 2u) << "a crash-kind slot was offered as a match";
}

// A stored head above the tail (#534): however the store recovers, it takes
// the next record and never offers records it already uploaded.
TEST_F(FdnRecordStoreTests, headAboveTailTakesTheNextRecordAndOffersNoUploadedOne) {
    deliver(pdnA, matchFrame(1, 0x01));
    deliver(pdnA, matchFrame(2, 0x02));
    store->getMatchRecords().retireOldest(2);
    storage.writeUInt(FDN_RECORD_STORE_NAMESPACE, "m_head", 40);
    restart();
    acks.clear();

    deliver(pdnA, matchFrame(3, 0x03));

    ASSERT_EQ(acks.size(), 1u) << "the store loaded as full and refused the record";
    FdnRecordStore::MatchRing::Entry entries[FDN_MATCH_RECORD_CAPACITY];
    ASSERT_EQ(store->getMatchRecords().peekOldest(entries, FDN_MATCH_RECORD_CAPACITY), 1u);
    EXPECT_EQ(entries[0].recordNumber, 3u);
}

// A lost head key leaves a pair wider than the ring; loading walks one ring, not the gap.
TEST_F(FdnRecordStoreTests, pairWiderThanTheRingLoadsAsAtMostAFullRing) {
    storage.writeUInt(FDN_RECORD_STORE_NAMESPACE, "m_tail", 100);
    restart();

    EXPECT_LE(store->getMatchRecords().getStoredCount(), FDN_MATCH_RECORD_CAPACITY);
}

TEST_F(FdnRecordStoreTests, ownCrashIsStoredUnderTheFdnMac) {
    const uint8_t fdnMac[6] = {0xF0, 0x0D, 0x00, 0x00, 0x00, 0x01};
    FdnRecordStore::CrashRing::Entry own{};
    memcpy(own.senderMac, fdnMac, 6);
    own.kind = static_cast<uint8_t>(RecordKind::CRASH);
    own.recordNumber = 12;
    ASSERT_TRUE(store->getCrashRecords().store(own));

    FdnRecordStore::CrashRing::Entry entries[1];
    ASSERT_EQ(store->getCrashRecords().peekOldest(entries, 1), 1u);
    EXPECT_EQ(memcmp(entries[0].senderMac, fdnMac, 6), 0);
    EXPECT_EQ(entries[0].recordNumber, 12u);
    EXPECT_TRUE(acks.empty());
}

TEST_F(FdnRecordStoreTests, olderFirmwareMatchesNamespaceIsUntouched) {
    storage.write(LEGACY_MATCHES_NAMESPACE, "count", "2");
    storage.write(LEGACY_MATCHES_NAMESPACE, "match_0", "{\"match_id\":\"a\"}");
    storage.write(LEGACY_MATCHES_NAMESPACE, "match_1", "{\"match_id\":\"b\"}");

    for (uint32_t number = 0; number < FDN_MATCH_RECORD_CAPACITY + 2; ++number) {
        deliver(pdnA, matchFrame(number, static_cast<uint8_t>(number)));
        deliver(pdnA, crashFrame(number, static_cast<uint8_t>(number)));
    }
    store->getMatchRecords().retireOldest(FDN_MATCH_RECORD_CAPACITY);
    restart();

    EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, "count", ""), "2");
    EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, "match_0", ""), "{\"match_id\":\"a\"}");
    EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, "match_1", ""), "{\"match_id\":\"b\"}");
}

TEST(FdnBeaconTest, beaconCarriesTheSenderDeviceType) {
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    testing::NiceMock<MockPeerComms> peerComms;
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ON_CALL(peerComms, getGlobalBroadcastAddress()).WillByDefault(testing::Return(broadcast));
    std::vector<std::vector<uint8_t>> beacons;
    ON_CALL(peerComms, sendData(testing::_, PktType::kPlayerInfoBroadcast, testing::_, testing::_))
    .WillByDefault(
    testing::Invoke([&beacons](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        beacons.emplace_back(data, data + len);
        return 0;
    }));
    RemotePlayerManager remotePlayerManager(&peerComms);
    Player fdnPlayer;

    clock.setTime(1);
    remotePlayerManager.StartBroadcastingPlayerInfo(&fdnPlayer, 12000, DeviceType::FDN);
    clock.advance(12000);
    remotePlayerManager.Update();

    ASSERT_EQ(beacons.size(), 2u);
    EXPECT_EQ(beacons[0].back(), static_cast<uint8_t>(DeviceType::FDN));
    EXPECT_EQ(beacons[1].back(), static_cast<uint8_t>(DeviceType::FDN));
    SimpleTimer::setPlatformClock(nullptr);
}
