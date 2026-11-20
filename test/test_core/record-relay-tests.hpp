#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "alleycat-server/fdn-record-store.hpp"
#include "device-mock.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "fdn-record-store-tests.hpp"
#include "game/game-session.hpp"
#include "game/match-manager.hpp"
#include "game/record-relay.hpp"
#include "game/shootout-manager.hpp"
#include "match-manager-tests.hpp"
#include "quickdraw-tests.hpp"
#include "utility-tests.hpp"
#include "wireless/remote-player-manager.hpp"

// A simulated radio that logs every send and can lose a packet type on the
// air: a lost frame never arrives and never gets a delivery report.
class RecordingPeerComms : public NativePeerCommsDriver {
public:
    using NativePeerCommsDriver::NativePeerCommsDriver;

    struct SentPacket {
        PktType type;
        std::array<uint8_t, 6> destination;
        std::vector<uint8_t> bytes;
    };

    /** Logs the send, then drops it if its type is lost, else hands it to the broker. */
    int sendData(const uint8_t* destination, PktType packetType, const uint8_t* data,
                 const size_t length) override {
        SentPacket packet{packetType, {}, std::vector<uint8_t>(data, data + length)};
        memcpy(packet.destination.data(), destination, 6);
        sent.push_back(packet);
        if (lostTypes.count(packetType) != 0) return 0;
        return NativePeerCommsDriver::sendData(destination, packetType, data, length);
    }

    /** The sends of @p type, oldest first. */
    std::vector<SentPacket> sentOf(PktType type) const {
        std::vector<SentPacket> matching;
        for (const SentPacket& packet : sent) {
            if (packet.type == type) matching.push_back(packet);
        }
        return matching;
    }

    std::vector<SentPacket> sent;
    std::set<PktType> lostTypes;
};

// Crash records 1..newest, each 42 bytes of its own number.
class FakeCrashSource : public RecordSource<CrashRecordTraits> {
public:
    /** Offers the lowest record above the mark. */
    bool readLowestPending(uint32_t& recordNumber,
                           uint8_t (&recordBytes)[CrashRecordTraits::RECORD_SIZE]) override {
        uint32_t reported = 0;
        return findLowestReadableRecord(
        mark, newest, MAX_CRASH_ENTRIES, reported, recordNumber,
        [&recordBytes](uint32_t number) {
            memset(recordBytes, static_cast<int>(number), sizeof(recordBytes));
            return true;
        },
        [](uint32_t) {});
    }

    /** Moves the mark as CrashLogger does, unless the test makes the write fail. */
    bool advanceMark(uint32_t recordNumber) override {
        if (refuseMark || advancedRecordMark(mark, newest, recordNumber) == mark) return false;
        mark = recordNumber;
        return true;
    }

    uint32_t newest = 0;
    uint32_t mark = 0;
    bool refuseMark = false;
};

// A PDN's relay, over the native broker, beside a real FDN store.
class RecordRelayTests : public testing::Test {
public:
    /** Brings up both radios, the PDN's managers and relay, and the FDN's store. */
    void SetUp() override {
        clock.setTime(1000);
        SimpleTimer::setPlatformClock(&clock);
        for (RecordingPeerComms* radio : {&pdnRadio, &fdnRadio}) {
            radio->initialize();
            radio->enable();
        }
        memcpy(pdnMac, pdnRadio.getMacAddress(), 6);
        memcpy(fdnMac, fdnRadio.getMacAddress(), 6);
        pdnWireless = new WirelessManager(&pdnRadio, nullptr);
        fdnWireless = new WirelessManager(&fdnRadio, nullptr);
        matchManager = new MatchManager(pdnWireless);
        matchManager->initialize(&player, &pdnStorage);
        shootoutManager = new ShootoutManager(&player, pdnWireless, &rdc);
        startRelay();
        store = new FdnRecordStore(fdnWireless, &fdnStorage);
    }

    /** Frees everything that registered with a radio before the radios go. */
    void TearDown() override {
        NativePeerBroker::getInstance().deliverPackets();
        delete store;
        delete relay;
        delete shootoutManager;
        delete matchManager;
        delete fdnWireless;
        delete pdnWireless;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /** Builds the relay over this PDN's crash and match records. */
    void startRelay() {
        relay = new RecordRelay(pdnWireless, matchManager, shootoutManager);
        relay->setCrashRecordSource(&crashSource);
    }

    /** Appends a match record of all @p fill bytes to the PDN's ring, as MatchManager does. */
    void storeMatch(uint8_t fill) {
        const uint32_t number = pdnStorage.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0) + 1;
        uint8_t record[MATCH_BINARY_SIZE];
        memset(record, fill, sizeof(record));
        const std::string key = "match_" + std::to_string((number - 1) % 32);
        pdnStorage.writeBytes(MATCH_RING_PREFS_NAMESPACE, key, record, sizeof(record));
        pdnStorage.writeUInt(MATCH_RING_PREFS_NAMESPACE, "seq", number);
    }

    /** The PDN's persisted collected mark. */
    uint32_t collectedSeq() {
        return pdnStorage.readUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", 0);
    }

    /** Puts a beacon from @p senderMac on the air to the PDN, as an FDN's RemotePlayerManager sends
     * it. */
    void airBeacon(const uint8_t* senderMac, DeviceType senderDeviceType = DeviceType::FDN) {
        PlayerInfoPkt packet{};
        packet.senderDeviceType = static_cast<uint8_t>(senderDeviceType);
        NativePeerBroker::getInstance().sendPacket(senderMac, pdnMac, PktType::kPlayerInfoBroadcast,
                                                   reinterpret_cast<const uint8_t*>(&packet),
                                                   sizeof(packet));
    }

    /** A beacon, then both loops until the air is quiet. */
    void beacon(const uint8_t* senderMac, DeviceType senderDeviceType = DeviceType::FDN) {
        airBeacon(senderMac, senderDeviceType);
        pump();
    }

    /** An ack from @p senderMac; each gets a fresh seqId so the channel's dedup passes it. */
    void ack(const uint8_t* senderMac, RecordKind kind, uint32_t number, uint16_t contentCrc) {
        RecordAckPacket packet{nextAckSeqId++, static_cast<uint8_t>(kind), number, contentCrc};
        sendToPdn(senderMac, PktType::kRecordAck, &packet, sizeof(packet));
    }

    /** Puts a packet from @p senderMac on the air to the PDN, then runs both loops. */
    void sendToPdn(const uint8_t* senderMac, PktType type, const void* data, size_t length) {
        NativePeerBroker::getInstance().sendPacket(senderMac, pdnMac, type,
                                                   static_cast<const uint8_t*>(data), length);
        pump();
    }

    /** Runs both devices' loops until the air is quiet; a record and its ack take two rounds. */
    void pump() {
        for (int round = 0; round < 16; ++round) {
            NativePeerBroker::getInstance().deliverPackets();
            fdnRadio.exec();
            pdnRadio.exec();
            relay->sync();
            store->sync();
        }
    }

    /** The content CRC of the frame of @p type the PDN sent @p index-th. */
    uint16_t sentContentCrc(PktType type, size_t index) {
        const std::vector<uint8_t> bytes = pdnRadio.sentOf(type).at(index).bytes;
        return crc16(bytes.data() + 6, bytes.size() - 6);
    }

    /** The record number of the frame of @p type the PDN sent @p index-th. */
    uint32_t sentRecordNumber(PktType type, size_t index) {
        uint32_t number = 0;
        memcpy(&number, pdnRadio.sentOf(type).at(index).bytes.data() + 2, sizeof(number));
        return number;
    }

    FakePlatformClock clock;
    RefusingPrefsDriver pdnStorage{"pdn-prefs",
                                   {MATCH_RING_PREFS_NAMESPACE, LEGACY_MATCHES_NAMESPACE}};
    NativePrefsDriver fdnStorage{"fdn-prefs", {FDN_RECORD_STORE_NAMESPACE}};
    RecordingPeerComms pdnRadio{"relay-pdn-radio"};
    RecordingPeerComms fdnRadio{"relay-fdn-radio"};
    uint8_t pdnMac[6] = {};
    uint8_t fdnMac[6] = {};
    // Beacons like an FDN but runs no store, so only the test acks for it.
    const uint8_t silentFdnMac[6] = {0x02, 0xFD, 0x00, 0x00, 0x00, 0x01};
    WirelessManager* pdnWireless = nullptr;
    WirelessManager* fdnWireless = nullptr;
    Player player;
    FakeRemoteDeviceCoordinator rdc;
    MatchManager* matchManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    FakeCrashSource crashSource;
    RecordRelay* relay = nullptr;
    FdnRecordStore* store = nullptr;
    uint8_t nextAckSeqId = 1;
};

TEST_F(RecordRelayTests, beaconRelaysEveryPendingRecordToTheRealStore) {
    crashSource.newest = 2;
    storeMatch(0xA1);
    storeMatch(0xA2);

    beacon(fdnMac);

    // One record of each kind, then the next after each ack, all to the beacon's sender.
    for (const RecordingPeerComms::SentPacket& frame : pdnRadio.sent) {
        if (frame.type == PktType::kCrashRecord || frame.type == PktType::kMatchRecord) {
            EXPECT_EQ(memcmp(frame.destination.data(), fdnMac, 6), 0);
        }
    }
    EXPECT_EQ(crashSource.mark, 2u);
    EXPECT_EQ(collectedSeq(), 2u);
    FdnRecordStore::MatchRing::Entry matches[3];
    ASSERT_EQ(store->getMatchRecords().peekOldest(matches, 3), 2u);
    EXPECT_EQ(memcmp(matches[0].senderMac, pdnMac, 6), 0);
    EXPECT_EQ(matches[0].recordNumber, 1u);
    EXPECT_EQ(matches[0].content[0], 0xA1);
    EXPECT_EQ(matches[1].recordNumber, 2u);
    EXPECT_EQ(store->getCrashRecords().getStoredCount(), 2u);
    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 2u);
    EXPECT_EQ(pdnRadio.sentOf(PktType::kMatchRecord).size(), 2u);
}

TEST_F(RecordRelayTests, ignoresBeaconsFromAnythingButAnFdn) {
    crashSource.newest = 1;
    storeMatch(0x01);

    beacon(fdnMac, DeviceType::PDN);
    beacon(fdnMac, DeviceType::UNKNOWN);
    const uint8_t shortBeacon[4] = {};
    sendToPdn(fdnMac, PktType::kPlayerInfoBroadcast, shortBeacon, sizeof(shortBeacon));

    EXPECT_TRUE(pdnRadio.sentOf(PktType::kCrashRecord).empty());
    EXPECT_TRUE(pdnRadio.sentOf(PktType::kMatchRecord).empty());
}

TEST_F(RecordRelayTests, sendsNothingWhileAMatchIsActive) {
    crashSource.newest = 1;
    storeMatch(0x01);
    matchManager->getCurrentMatch().emplace("active-match");

    beacon(fdnMac);
    EXPECT_TRUE(pdnRadio.sentOf(PktType::kCrashRecord).empty());
    EXPECT_TRUE(pdnRadio.sentOf(PktType::kMatchRecord).empty());

    matchManager->clearCurrentMatch();
    beacon(fdnMac);
    EXPECT_EQ(crashSource.mark, 1u);
    EXPECT_EQ(collectedSeq(), 1u);
}

TEST_F(RecordRelayTests, sendsNothingWhileAShootoutIsActive) {
    crashSource.newest = 1;
    shootoutManager->startProposal();
    ASSERT_TRUE(shootoutManager->active());

    beacon(fdnMac);

    EXPECT_TRUE(pdnRadio.sentOf(PktType::kCrashRecord).empty());
}

TEST_F(RecordRelayTests, ackDuringAMatchRetiresButSendsNoMore) {
    crashSource.newest = 2;
    beacon(silentFdnMac);
    ASSERT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);
    matchManager->getCurrentMatch().emplace("active-match");

    ack(silentFdnMac, RecordKind::CRASH, 1, sentContentCrc(PktType::kCrashRecord, 0));

    EXPECT_EQ(crashSource.mark, 1u);
    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);

    // An ack already answered is not a cue to send, even once play is over.
    matchManager->clearCurrentMatch();
    ack(silentFdnMac, RecordKind::CRASH, 1, sentContentCrc(PktType::kCrashRecord, 0));
    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);
}

TEST_F(RecordRelayTests, markThatFailsToMoveIsNotResentStraightBack) {
    crashSource.newest = 2;
    crashSource.refuseMark = true;
    beacon(silentFdnMac);

    ack(silentFdnMac, RecordKind::CRASH, 1, sentContentCrc(PktType::kCrashRecord, 0));

    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);
}

// The PDN's own ring: a refused mark write leaves the record pending, so the
// relay does not send it straight back for the FDN to ack again.
TEST_F(RecordRelayTests, refusedMatchMarkWriteIsNotResentStraightBack) {
    storeMatch(0x01);
    storeMatch(0x02);
    beacon(silentFdnMac);
    ASSERT_EQ(pdnRadio.sentOf(PktType::kMatchRecord).size(), 1u);

    pdnStorage.refuseUInt = true;
    ack(silentFdnMac, RecordKind::MATCH, 1, sentContentCrc(PktType::kMatchRecord, 0));
    pdnStorage.refuseUInt = false;

    EXPECT_EQ(pdnRadio.sentOf(PktType::kMatchRecord).size(), 1u);
    beacon(silentFdnMac);
    ASSERT_EQ(pdnRadio.sentOf(PktType::kMatchRecord).size(), 2u);
    EXPECT_EQ(sentRecordNumber(PktType::kMatchRecord, 1), 1u) << "match 1 is still pending";
}

// Every slot holds a collected record. A refused record write moves no number,
// so the old record in the next slot is never relayed as the new match.
TEST_F(RecordRelayTests, refusedMatchWriteRelaysNoCollectedRecordAsNew) {
    for (uint32_t number = 1; number <= MatchManager::MATCH_RING_CAPACITY; ++number) {
        storeMatch(static_cast<uint8_t>(number));
    }
    pdnStorage.writeUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq",
                         MatchManager::MATCH_RING_CAPACITY);

    pdnStorage.refuseBytes = true;
    matchManager->getCurrentMatch().emplace("refused-write-match");
    EXPECT_FALSE(matchManager->finalizeMatch());
    pdnStorage.refuseBytes = false;
    beacon(silentFdnMac);

    EXPECT_TRUE(pdnRadio.sentOf(PktType::kMatchRecord).empty())
    << "collected match 1 went out as match 33";
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), MatchManager::MATCH_RING_CAPACITY);
}

TEST(RecordSourceTests, markOnlyRisesAndPendingStartsInsideTheRing) {
    EXPECT_EQ(advancedRecordMark(3, 5, 4), 4u);
    EXPECT_EQ(advancedRecordMark(3, 5, 2), 3u);
    EXPECT_EQ(advancedRecordMark(3, 5, 6), 3u);

    // A 10-slot ring holding 6..15 with nothing acked; 6 and 7 cannot be read.
    uint32_t reported = 0;
    uint32_t number = 0;
    std::vector<uint32_t> unreadable;
    for (int pass = 0; pass < 2; ++pass) {
        EXPECT_TRUE(findLowestReadableRecord(
        0, 15, 10, reported, number, [](uint32_t n) { return n >= 8; },
        [&unreadable](uint32_t n) { unreadable.push_back(n); }));
        EXPECT_EQ(number, 8u);
    }
    EXPECT_EQ(unreadable, (std::vector<uint32_t>{6, 7}));
    EXPECT_FALSE(findLowestReadableRecord(
    15, 15, 10, reported, number, [](uint32_t) { return true; }, [](uint32_t) {}));
}

TEST_F(RecordRelayTests, oneFrameOfEachKindInFlightUnderABeaconBurst) {
    crashSource.newest = 3;
    storeMatch(0x01);
    pdnRadio.lostTypes = {PktType::kCrashRecord, PktType::kMatchRecord};

    beacon(fdnMac);
    beacon(fdnMac);
    beacon(silentFdnMac);

    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);
    EXPECT_EQ(pdnRadio.sentOf(PktType::kMatchRecord).size(), 1u);

    // Once the transport abandons the frame, the next beacon may send it again.
    clock.advance(Resender::STALE_AFTER_MS);
    pump();
    const size_t crashFramesBefore = pdnRadio.sentOf(PktType::kCrashRecord).size();
    beacon(fdnMac);
    ASSERT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), crashFramesBefore + 1);
    EXPECT_EQ(sentRecordNumber(PktType::kCrashRecord, crashFramesBefore), 1u);
}

TEST_F(RecordRelayTests, ackMismatchedOnAnyFieldIsIgnored) {
    crashSource.newest = 2;
    beacon(silentFdnMac);
    const uint16_t crc = sentContentCrc(PktType::kCrashRecord, 0);

    ack(silentFdnMac, RecordKind::MATCH, 1, crc);
    ack(silentFdnMac, RecordKind::CRASH, 2, crc);
    ack(silentFdnMac, RecordKind::CRASH, 1, static_cast<uint16_t>(crc ^ 1));
    ack(fdnMac, RecordKind::CRASH, 1, crc);
    EXPECT_EQ(crashSource.mark, 0u);
    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);

    // The matching ack retires record 1 and sends record 2 at once, to the same FDN.
    ack(silentFdnMac, RecordKind::CRASH, 1, crc);
    EXPECT_EQ(crashSource.mark, 1u);
    ASSERT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 2u);
    EXPECT_EQ(sentRecordNumber(PktType::kCrashRecord, 1), 2u);
    EXPECT_EQ(memcmp(pdnRadio.sentOf(PktType::kCrashRecord)[1].destination.data(), silentFdnMac, 6),
              0);

    // The same ack again advances nothing.
    ack(silentFdnMac, RecordKind::CRASH, 1, crc);
    EXPECT_EQ(crashSource.mark, 1u);
}

TEST_F(RecordRelayTests, deliveryReportAloneLeavesTheRecordPending) {
    crashSource.newest = 1;
    storeMatch(0x01);

    beacon(silentFdnMac);
    EXPECT_EQ(crashSource.mark, 0u);
    EXPECT_EQ(collectedSeq(), 0u);

    // Delivered, so no longer in flight: the next beacon sends the same records.
    beacon(silentFdnMac);
    ASSERT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 2u);
    EXPECT_EQ(sentRecordNumber(PktType::kCrashRecord, 1), 1u);
    ASSERT_EQ(pdnRadio.sentOf(PktType::kMatchRecord).size(), 2u);
    EXPECT_EQ(sentRecordNumber(PktType::kMatchRecord, 1), 1u);
}

TEST_F(RecordRelayTests, fullFdnStoreSendsNoAckAndTheRecordStaysPending) {
    FdnRecordStore::CrashRing::Entry other{};
    other.kind = static_cast<uint8_t>(RecordKind::CRASH);
    for (uint32_t number = 1; number <= FDN_CRASH_RECORD_CAPACITY; ++number) {
        other.recordNumber = number;
        ASSERT_TRUE(store->getCrashRecords().store(other));
    }
    crashSource.newest = 1;

    beacon(fdnMac);

    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);
    EXPECT_TRUE(fdnRadio.sentOf(PktType::kRecordAck).empty());
    EXPECT_EQ(crashSource.mark, 0u);
}

TEST_F(RecordRelayTests, restartBeforeTheAckResendsAndTheFdnAcksItsCopy) {
    storeMatch(0x01);
    airBeacon(fdnMac);
    NativePeerBroker::getInstance().deliverPackets();
    pdnRadio.exec();
    NativePeerBroker::getInstance().deliverPackets();
    fdnRadio.exec();
    ASSERT_EQ(store->getMatchRecords().getStoredCount(), 1u);

    // The PDN restarts with the FDN's ack still on the air.
    delete relay;
    startRelay();
    pump();
    EXPECT_EQ(collectedSeq(), 0u);

    // Two beacons: the restarted channel's random first seqId can equal the one
    // the FDN last saw, and the FDN drops that frame as a duplicate; the next
    // beacon resends it under a new seqId.
    beacon(fdnMac);
    beacon(fdnMac);

    EXPECT_EQ(collectedSeq(), 1u);
    EXPECT_EQ(store->getMatchRecords().getStoredCount(), 1u);
}

TEST_F(RecordRelayTests, crashAndMatchLanesDoNotHoldEachOtherBack) {
    crashSource.newest = 1;
    storeMatch(0x01);
    storeMatch(0x02);
    pdnRadio.lostTypes = {PktType::kCrashRecord};

    beacon(fdnMac);

    EXPECT_EQ(collectedSeq(), 2u);
    EXPECT_EQ(crashSource.mark, 0u);
    EXPECT_EQ(pdnRadio.sentOf(PktType::kCrashRecord).size(), 1u);
}

TEST_F(RecordRelayTests, unreadableMatchIsPassedOverAndCollectedPast) {
    storeMatch(0x01);
    storeMatch(0x02);
    const uint8_t torn[4] = {};
    pdnStorage.writeBytes(MATCH_RING_PREFS_NAMESPACE, "match_0", torn, sizeof(torn));

    beacon(fdnMac);

    ASSERT_EQ(pdnRadio.sentOf(PktType::kMatchRecord).size(), 1u);
    EXPECT_EQ(sentRecordNumber(PktType::kMatchRecord, 0), 2u);
    EXPECT_EQ(collectedSeq(), 2u);
}

TEST_F(RecordRelayTests, relayLeavesOlderFirmwareMatchesByteIdentical) {
    const std::vector<std::pair<std::string, std::string>> legacy = {
    {"count", "2"},
    {"test_key", "1"},
    {"match_0", "{\"match_id\":\"a\",\"hunter\":\"1111\",\"bounty\":\"2222\"}"},
    {"match_1", "{\"match_id\":\"b\",\"hunter\":\"3333\",\"bounty\":\"4444\"}"},
    };
    for (const auto& entry : legacy) {
        pdnStorage.write(LEGACY_MATCHES_NAMESPACE, entry.first, entry.second);
    }
    crashSource.newest = 1;
    storeMatch(0x01);

    beacon(fdnMac);
    ASSERT_EQ(collectedSeq(), 1u);

    for (const auto& entry : legacy) {
        EXPECT_EQ(pdnStorage.read(LEGACY_MATCHES_NAMESPACE, entry.first, ""), entry.second);
    }
    EXPECT_EQ(pdnStorage.read(LEGACY_MATCHES_NAMESPACE, "match_2", "absent"), "absent");
}

// The session builds the relay over its own match ring, takes the crash source,
// and drives the relay's retries from the device tick.
TEST_F(GameSessionLifecycleTests, sessionRelaysBothKindsOnAnFdnBeaconAndRetriesFromItsTick) {
    MockPeerComms& radio = *device.mockPeerComms;
    PeerCommsInterface::PacketCallback beaconHandler = nullptr;
    void* beaconContext = nullptr;
    ON_CALL(radio, setPacketHandler(testing::Eq(PktType::kPlayerInfoBroadcast), _, _))
    .WillByDefault(
    testing::DoAll(testing::SaveArg<1>(&beaconHandler), testing::SaveArg<2>(&beaconContext)));
    ON_CALL(radio, getPeerCommsState()).WillByDefault(Return(PeerCommsState::ENABLED));
    std::vector<PktType> frames;
    ON_CALL(radio, sendData(_, _, _, _))
    .WillByDefault([&frames](const uint8_t*, PktType type, const uint8_t*, const size_t) {
        if (type == PktType::kCrashRecord || type == PktType::kMatchRecord) frames.push_back(type);
        return 0;
    });
    // Match 1 waits in the ring.
    ON_CALL(*device.mockStorage,
            readUInt(testing::Eq(MATCH_RING_PREFS_NAMESPACE), testing::Eq(std::string("seq")), _))
    .WillByDefault(Return(1));
    ON_CALL(*device.mockStorage, readBytes(testing::Eq(MATCH_RING_PREFS_NAMESPACE), _, _, _))
    .WillByDefault(Return(MATCH_BINARY_SIZE));
    FakeCrashSource crashSource;
    crashSource.newest = 1;

    GameSession* session = new GameSession(player, &device, nullptr);
    session->setCrashRecordSource(&crashSource);
    ASSERT_NE(beaconHandler, nullptr);
    PlayerInfoPkt beacon{};
    beacon.senderDeviceType = static_cast<uint8_t>(DeviceType::FDN);
    const uint8_t fdnMac[6] = {0x02, 0xFD, 0x00, 0x00, 0x00, 0x02};
    beaconHandler(fdnMac, reinterpret_cast<const uint8_t*>(&beacon), sizeof(beacon), beaconContext);
    EXPECT_EQ(std::count(frames.begin(), frames.end(), PktType::kCrashRecord), 1);
    EXPECT_EQ(std::count(frames.begin(), frames.end(), PktType::kMatchRecord), 1);

    fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    device.loop();
    EXPECT_EQ(std::count(frames.begin(), frames.end(), PktType::kCrashRecord), 2);

    delete session;
}
