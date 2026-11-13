#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <new>
#include <vector>

#include "alleycat-server/fdn-record-store.hpp"
#include "device/crash/crash-record.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device-mock.hpp"
#include "game/game-session.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "game/record-relay.hpp"
#include "id-generator.hpp"
#include "game/shootout-manager.hpp"
#include "utility-tests.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/record-source.hpp"
#include "wireless/match-packet.hpp"
#include "wireless/remote-player-manager.hpp"

constexpr unsigned long RECORD_RELAY_BEACON_INTERVAL_MS = 12000;

/**
 * Records of one kind numbered above a mark, up to the newest. Every byte of a
 * record is its number, so two records never read alike.
 */
template <class Traits> class FakeRecordSource : public RecordSource<Traits> {
public:
    /** Hands out record mark + 1 while it is not above the newest. */
    bool readLowestPending(uint32_t& recordNumber,
                           uint8_t (&recordBytes)[Traits::RECORD_SIZE]) override {
        if (mark >= newestNumber) return false;
        recordNumber = mark + 1;
        memset(recordBytes, static_cast<uint8_t>(recordNumber), Traits::RECORD_SIZE);
        return true;
    }

    /**
     * Counts every call and moves the mark forwards only; not at all while
     * markWritesFail, as when the storage behind a real mark refuses the write.
     */
    void advanceMark(uint32_t recordNumber) override {
        advanceCalls++;
        if (markWritesFail) return;
        if (recordNumber > mark && recordNumber <= newestNumber) mark = recordNumber;
    }

    uint32_t newestNumber = 0;
    uint32_t mark = 0;
    size_t advanceCalls = 0;
    bool markWritesFail = false;
};

/**
 * One record frame as a radio was asked to send it. The header fields are
 * copied out because a field of the packed frame cannot be passed by reference.
 */
template <class Frame> struct SentRecordFrame {
    std::array<uint8_t, 6> toMac;
    size_t length;
    std::vector<uint8_t> bytes;
    /** The four below are decoded only when the frame had the length of a Frame. */
    uint8_t seqId;
    uint8_t kind;
    uint32_t recordNumber;
    Frame frame;
};

/** Every frame a mock radio was handed, in order. */
class RecordRelayRadioLog {
public:
    struct Sent {
        std::array<uint8_t, 6> toMac;
        PktType type;
        std::vector<uint8_t> bytes;
    };

    /** Records each frame @p peerComms is given, then tells onSent about it. */
    void attach(MockPeerComms& peerComms) {
        ON_CALL(peerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(
        [this](const uint8_t* toMac, PktType type, const uint8_t* data, const size_t length) {
            Sent frame;
            memcpy(frame.toMac.data(), toMac, 6);
            frame.type = type;
            frame.bytes.assign(data, data + length);
            sent.push_back(frame);
            if (onSent) onSent(frame);
            return 1;
        });
    }

    /** The frames sent on @p type, read as @p Frame. */
    template <class Frame> std::vector<SentRecordFrame<Frame>> framesOf(PktType type) const {
        std::vector<SentRecordFrame<Frame>> frames;
        for (const Sent& one : sent) {
            if (one.type != type) continue;
            SentRecordFrame<Frame> decoded{};
            decoded.toMac = one.toMac;
            decoded.length = one.bytes.size();
            decoded.bytes = one.bytes;
            if (one.bytes.size() == sizeof(Frame)) {
                memcpy(&decoded.frame, one.bytes.data(), sizeof(Frame));
                decoded.seqId = decoded.frame.seqId;
                decoded.kind = decoded.frame.kind;
                decoded.recordNumber = decoded.frame.recordNumber;
            }
            frames.push_back(decoded);
        }
        return frames;
    }

    std::vector<Sent> sent;
    std::function<void(const Sent&)> onSent;
};

inline std::array<uint8_t, 6> relayMacArray(const uint8_t* mac) {
    std::array<uint8_t, 6> result{};
    memcpy(result.data(), mac, 6);
    return result;
}

/**
 * A PDN with a game session, nothing Connected on its jacks, a fake crash record
 * source and a radio that records every frame. Beacons, jack connections,
 * delivery reports and stored acknowledgements are fed in as the radio and the
 * coordinator would deliver them. bringUpFdn() puts a real FDN record store on
 * the other side of the radio.
 */
class RecordRelayTests : public testing::Test {
public:
    /** Builds the session with the peer-to-peer radio enabled. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
        // Starting a match draws its ID from here.
        IdGenerator::initialize(42);

        ON_CALL(*pdnDevice.mockPeerComms, getPeerCommsState())
        .WillByDefault(testing::Return(PeerCommsState::ENABLED));
        ON_CALL(*pdnDevice.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(pdnMac));
        pdnRadio.attach(*pdnDevice.mockPeerComms);

        player = new Player();
        char playerId[] = "0042";
        player->setUserID(playerId);
        gameSession = new GameSession(player, &pdnDevice, nullptr);
        gameSession->setCrashRecordSource(&crashSource);
    }

    /** Releases the session and the store before the radios they are registered on. */
    void TearDown() override {
        delete gameSession;
        gameSession = nullptr;
        delete fdnRecordStore;
        fdnRecordStore = nullptr;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** The record frames of Traits' kind the PDN's radio was asked to send, in order. */
    template <class Traits> std::vector<SentRecordFrame<typename Traits::Frame>> framesOf() const {
        return pdnRadio.framesOf<typename Traits::Frame>(Traits::FRAME_PACKET_TYPE);
    }

    /** Delivers a player-info beacon from @p fromMac whose sender says it is a @p senderType. */
    void hearBeacon(const uint8_t* fromMac, DeviceType senderType = DeviceType::FDN,
                    size_t length = sizeof(PlayerInfoPkt)) {
        PlayerInfoPkt beacon{};
        beacon.senderDeviceType = static_cast<uint8_t>(senderType);
        ASSERT_TRUE(pdnDevice.mockPeerComms->triggerPacketCallback(
        fromMac, PktType::kPlayerInfoBroadcast, reinterpret_cast<const uint8_t*>(&beacon), length))
        << "nothing on the PDN listens for player-info beacons";
    }

    /** A device of @p peerType with address @p peerMac becomes Connected on @p jack. */
    void connectPeer(SerialIdentifier jack, DeviceType peerType, const uint8_t* peerMac) {
        FakeRemoteDeviceCoordinator& rdc = pdnDevice.fakeRemoteDeviceCoordinator;
        rdc.setPeerDeviceType(jack, peerType);
        rdc.setPeerMac(jack, peerMac);
        rdc.setJackStatus(jack, JackStatus::CONNECTED);
    }

    /** The radio's report that @p sent reached the device it was addressed to. */
    template <class Traits>
    void reportDelivered(const SentRecordFrame<typename Traits::Frame>& sent) {
        pdnDevice.wirelessManager->getReliableTransport()->onSendResult(
        Traits::FRAME_PACKET_TYPE, sent.toMac.data(), reinterpret_cast<const uint8_t*>(&sent.frame),
        sizeof(sent.frame), true);
    }

    /** The stored acknowledgement an FDN sends for @p frame. */
    template <class Traits>
    static RecordAckPacket acknowledgementFor(const typename Traits::Frame& frame) {
        RecordAckPacket ack{};
        ack.kind = frame.kind;
        ack.recordNumber = frame.recordNumber;
        computeRecordCheck(frame.recordBytes, sizeof(frame.recordBytes), frame.trailer,
                           ack.recordCheck);
        return ack;
    }

    /**
     * Delivers @p ack to the PDN as if @p fromMac had sent it on the
     * acknowledgement packet type of Traits' kind. Every delivery is a new
     * send, so it carries a new sequence ID.
     */
    template <class Traits>
    void deliverAcknowledgement(const uint8_t* fromMac, RecordAckPacket ack) {
        nextSeqId = nextSeqId == 255 ? 1 : nextSeqId + 1;
        ack.seqId = nextSeqId;
        ASSERT_TRUE(pdnDevice.mockPeerComms->triggerPacketCallback(
        fromMac, Traits::ACK_PACKET_TYPE, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack)))
        << "nothing on the PDN listens for stored acknowledgements";
    }

    /** Starts a match the way the cable handshake does. */
    void startMatch() {
        gameSession->getContext().matchManager->initializeMatch(opponentMac);
        ASSERT_TRUE(gameSession->getContext().matchManager->getCurrentMatch().has_value());
    }

    /** Ends the match startMatch() began. */
    void endMatch() { gameSession->getContext().matchManager->clearCurrentMatch(); }

    /**
     * Puts an FDN with a real record store in radio range: from here on every
     * frame either device sends waits on the air until pumpAir() delivers it.
     */
    void bringUpFdn() {
        ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
        .WillByDefault(testing::Return(PeerCommsState::ENABLED));
        fdnRadio.attach(*fdnDevice.mockPeerComms);
        pdnRadio.onSent = [this](const RecordRelayRadioLog::Sent& frame) {
            air.push_back({true, frame});
        };
        fdnRadio.onSent = [this](const RecordRelayRadioLog::Sent& frame) {
            air.push_back({false, frame});
        };
        fdnRecordStore = new FdnRecordStore(fdnDevice.wirelessManager, &fdnPrefs);
    }

    /**
     * Delivers the frames on the air, and those sent in answer, until none is
     * left. A unicast is reported delivered to its sender first, as the radio
     * does. A frame addressed to neither device is lost.
     */
    void pumpAir() {
        while (!air.empty()) {
            const AirFrame onAir = air.front();
            air.pop_front();
            const RecordRelayRadioLog::Sent& frame = onAir.frame;
            MockDevice& sender = onAir.fromPdn ? pdnDevice : fdnDevice;
            MockDevice& receiver = onAir.fromPdn ? fdnDevice : pdnDevice;
            const uint8_t* senderMac = onAir.fromPdn ? pdnMac : fdnMac;
            const uint8_t* receiverMac = onAir.fromPdn ? fdnMac : pdnMac;
            const bool isBroadcast = memcmp(frame.toMac.data(), MockDevice::BROADCAST_MAC, 6) == 0;
            if (!isBroadcast && memcmp(frame.toMac.data(), receiverMac, 6) != 0) continue;
            if (!isBroadcast) {
                sender.wirelessManager->getReliableTransport()->onSendResult(
                frame.type, frame.toMac.data(), frame.bytes.data(), frame.bytes.size(), true);
            }
            receiver.mockPeerComms->triggerPacketCallback(senderMac, frame.type, frame.bytes.data(),
                                                          frame.bytes.size());
        }
    }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice pdnDevice;
    MockDevice fdnDevice;
    NativePrefsDriver fdnPrefs{"relay-fdn-prefs", {FDN_RECORD_STORE_NAMESPACE}};
    RecordRelayRadioLog pdnRadio;
    RecordRelayRadioLog fdnRadio;
    FakeRecordSource<CrashRecordTraits> crashSource;
    Player* player = nullptr;
    GameSession* gameSession = nullptr;
    FdnRecordStore* fdnRecordStore = nullptr;
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t fdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF1};
    uint8_t otherFdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF2};
    uint8_t strangerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x66};
    uint8_t opponentMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    uint8_t nextSeqId = 0;

private:
    struct AirFrame {
        bool fromPdn;
        RecordRelayRadioLog::Sent frame;
    };
    std::deque<AirFrame> air;
};

// ---- Relay target and triggers ----

inline void
recordRelaySendsTheLowestPendingCrashRecordToTheFdnWhoseBeaconItHeard(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 7;
    suite->crashSource.mark = 6;
    ASSERT_EQ(
    suite->pdnDevice.fakeRemoteDeviceCoordinator.getJackStatus(SerialIdentifier::OUTPUT_JACK),
    JackStatus::DISCONNECTED);

    suite->hearBeacon(suite->fdnMac);

    const std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].toMac, relayMacArray(suite->fdnMac));
    ASSERT_EQ(frames[0].length, 65u) << "a crash record goes out as one 65-byte frame";

    // Kind, number, record bytes, trailer, in that order after the sequence ID.
    const std::vector<uint8_t>& bytes = frames[0].bytes;
    EXPECT_EQ(bytes[1], 0x01);
    uint32_t recordNumber = 0;
    memcpy(&recordNumber, &bytes[2], sizeof(recordNumber));
    EXPECT_EQ(recordNumber, 7u);
    for (size_t i = 6; i < 6 + CRASH_RECORD_SIZE; i++) {
        ASSERT_EQ(bytes[i], 7) << "record byte " << (i - 6) << " is not the source's";
    }
    // No record key in this build: key ID 0 and 16 zero bytes.
    for (size_t i = 6 + CRASH_RECORD_SIZE; i < bytes.size(); i++) {
        ASSERT_EQ(bytes[i], 0) << "trailer byte " << (i - 6 - CRASH_RECORD_SIZE);
    }
    EXPECT_EQ(suite->crashSource.mark, 6u) << "sending a record must not retire it";
}

inline void recordRelayIgnoresABeaconThatDoesNotIdentifyAnFdn(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;

    suite->hearBeacon(suite->strangerMac, DeviceType::PDN);
    suite->hearBeacon(suite->strangerMac, DeviceType::UNKNOWN);
    // Too short to carry the byte that says who sent it.
    suite->hearBeacon(suite->fdnMac, DeviceType::FDN, sizeof(PlayerInfoPkt) - 1);

    EXPECT_TRUE(suite->framesOf<CrashRecordTraits>().empty());
    EXPECT_EQ(suite->crashSource.mark, 0u);
}

inline void recordRelaySendsWhenAnFdnBecomesConnectedOnAJack(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;

    // The other observers of a PDN connecting read its profile.
    suite->pdnDevice.fakeRemoteDeviceCoordinator.setPeerRole(SerialIdentifier::INPUT_JACK, true);
    suite->connectPeer(SerialIdentifier::INPUT_JACK, DeviceType::PDN, suite->opponentMac);
    EXPECT_TRUE(suite->framesOf<CrashRecordTraits>().empty())
    << "a PDN on a jack is not a relay target";

    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].toMac, relayMacArray(suite->fdnMac));
    EXPECT_EQ(frames[0].recordNumber, 1u);

    // Becoming Connected is the trigger; staying Connected is not.
    suite->reportDelivered<CrashRecordTraits>(frames[0]);
    for (int tick = 0; tick < 5; tick++) {
        suite->pdnDevice.loop();
    }
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);
}

inline void recordRelayDeliversBothPendingRecordsToAConnectedFdn(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].recordNumber, 1u);
    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[0].frame));

    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 2u);
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->fdnMac));
    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[1].frame));

    EXPECT_EQ(suite->crashSource.mark, 2u);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);
}

inline void recordRelaySendsNothingWithNoFdnInReach(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    for (int tick = 0; tick < 30; tick++) {
        suite->fakeClock->advance(1000);
        suite->pdnDevice.loop();
    }

    EXPECT_TRUE(suite->framesOf<CrashRecordTraits>().empty());
    EXPECT_EQ(suite->crashSource.mark, 0u);
}

inline void recordRelaySendsNothingWhenNoRecordIsPending(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 3;
    suite->crashSource.mark = 3;

    suite->hearBeacon(suite->fdnMac);
    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);

    EXPECT_TRUE(suite->framesOf<CrashRecordTraits>().empty());
}

inline void recordRelaySendsAnUnacknowledgedRecordAgainOnTheNextBeacon(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;

    suite->hearBeacon(suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    suite->reportDelivered<CrashRecordTraits>(frames[0]);

    suite->fakeClock->advance(RECORD_RELAY_BEACON_INTERVAL_MS);
    suite->pdnDevice.loop();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u) << "nothing is sent between beacons";

    suite->hearBeacon(suite->fdnMac);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 1u);
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->fdnMac));
    EXPECT_NE(frames[1].seqId, frames[0].seqId) << "a new send, not a retransmit";
    EXPECT_EQ(suite->crashSource.mark, 0u);
}

// ---- Mark advances only on the stored acknowledgement ----

inline void recordRelayKeepsTheMarkOnADeliveryReportAlone(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    suite->hearBeacon(suite->fdnMac);
    const std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    suite->reportDelivered<CrashRecordTraits>(frames[0]);
    suite->pdnDevice.loop();

    EXPECT_EQ(suite->crashSource.mark, 0u);
    EXPECT_EQ(suite->crashSource.advanceCalls, 0u);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the next record waits for the acknowledgement";
}

inline void recordRelayIgnoresAnAcknowledgementFromAnotherDevice(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    suite->hearBeacon(suite->fdnMac);
    const std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    suite->reportDelivered<CrashRecordTraits>(frames[0]);

    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->strangerMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[0].frame));

    EXPECT_EQ(suite->crashSource.mark, 0u);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);
}

inline void recordRelayIgnoresALateAcknowledgementFromAnEarlierFdn(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 3;
    suite->crashSource.mark = 1;

    suite->hearBeacon(suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].recordNumber, 2u);
    suite->reportDelivered<CrashRecordTraits>(frames[0]);

    suite->hearBeacon(suite->otherFdnMac);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 2u);
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->otherFdnMac));
    suite->reportDelivered<CrashRecordTraits>(frames[1]);

    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[0].frame));
    EXPECT_EQ(suite->crashSource.mark, 1u) << "the earlier FDN's acknowledgement moved the mark";
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);

    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->otherFdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[1].frame));
    EXPECT_EQ(suite->crashSource.mark, 2u);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 3u) << "crash record 3 is sent once";
    EXPECT_EQ(frames[2].recordNumber, 3u);
    EXPECT_EQ(frames[2].toMac, relayMacArray(suite->otherFdnMac));
}

inline void
recordRelayIgnoresAnAcknowledgementThatDoesNotMatchTheFrameItSent(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 4;
    suite->crashSource.mark = 2;

    suite->hearBeacon(suite->fdnMac);
    const std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].recordNumber, 3u);
    suite->reportDelivered<CrashRecordTraits>(frames[0]);
    const RecordAckPacket matching =
    RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[0].frame);

    // Same number, but the check of different bytes.
    RecordAckPacket wrongCheck = matching;
    wrongCheck.recordCheck[RECORD_CHECK_SIZE - 1] ^= 0x01;
    suite->deliverAcknowledgement<CrashRecordTraits>(suite->fdnMac, wrongCheck);

    RecordAckPacket wrongNumber = matching;
    wrongNumber.recordNumber = 4;
    suite->deliverAcknowledgement<CrashRecordTraits>(suite->fdnMac, wrongNumber);

    RecordAckPacket earlierNumber = matching;
    earlierNumber.recordNumber = 1;
    suite->deliverAcknowledgement<CrashRecordTraits>(suite->fdnMac, earlierNumber);

    RecordAckPacket wrongKind = matching;
    wrongKind.kind = static_cast<uint8_t>(RecordKind::MATCH);
    suite->deliverAcknowledgement<CrashRecordTraits>(suite->fdnMac, wrongKind);

    // The right acknowledgement on the other kind's packet type.
    suite->deliverAcknowledgement<MatchRecordTraits>(suite->fdnMac, matching);

    EXPECT_EQ(suite->crashSource.mark, 2u);
    EXPECT_EQ(suite->crashSource.advanceCalls, 0u);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);

    suite->deliverAcknowledgement<CrashRecordTraits>(suite->fdnMac, matching);
    EXPECT_EQ(suite->crashSource.mark, 3u) << "the record stayed pending, so its own "
                                              "acknowledgement still counts";
}

inline void
recordRelayAdvancesTheMarkAndSendsTheNextRecordToTheFdnThatAcknowledged(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    suite->hearBeacon(suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    const RecordAckPacket firstAck =
    RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[0].frame);

    // Before any delivery report: the acknowledgement is the evidence, not the radio.
    suite->deliverAcknowledgement<CrashRecordTraits>(suite->fdnMac, firstAck);
    EXPECT_EQ(suite->crashSource.mark, 1u);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 2u);
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->fdnMac));

    // The acknowledged frame is no longer retried.
    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    suite->pdnDevice.loop();
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[2].recordNumber, 2u) << "the retry was of the acknowledged record";
    EXPECT_EQ(frames[2].seqId, frames[1].seqId);

    // The FDN sends the first acknowledgement again: nothing is left for it to answer.
    suite->deliverAcknowledgement<CrashRecordTraits>(suite->fdnMac, firstAck);
    EXPECT_EQ(suite->crashSource.mark, 1u);
    EXPECT_EQ(suite->crashSource.advanceCalls, 1u);

    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[1].frame));
    EXPECT_EQ(suite->crashSource.mark, 2u);

    suite->hearBeacon(suite->fdnMac);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 3u) << "nothing is pending any more";
}

// ---- One record of each kind in flight ----

inline void recordRelayDoesNotMultiplySendsOnABurstOfBeacons(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    suite->hearBeacon(suite->fdnMac);
    ASSERT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);

    suite->hearBeacon(suite->fdnMac);
    suite->hearBeacon(suite->otherFdnMac);
    suite->hearBeacon(suite->fdnMac);
    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->otherFdnMac);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);

    // The transport is still retrying that one frame, to the FDN it was sent to.
    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    suite->pdnDevice.loop();
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].seqId, frames[0].seqId);
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->fdnMac));

    suite->hearBeacon(suite->fdnMac);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);

    // Abandoned: the next beacon is a new chance.
    suite->fakeClock->advance(Resender::RETRANSMIT_SPAN_MS);
    suite->pdnDevice.loop();
    ASSERT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);
    suite->hearBeacon(suite->otherFdnMac);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[2].recordNumber, 1u);
    EXPECT_EQ(frames[2].toMac, relayMacArray(suite->otherFdnMac));
    EXPECT_NE(frames[2].seqId, frames[0].seqId);
}

// ---- Play gate ----

inline void recordRelaySendsNothingDuringAMatchOrAShootout(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    suite->startMatch();
    suite->hearBeacon(suite->fdnMac);
    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    EXPECT_TRUE(suite->framesOf<CrashRecordTraits>().empty()) << "a record was sent during a match";

    suite->endMatch();
    suite->pdnDevice.loop();
    EXPECT_TRUE(suite->framesOf<CrashRecordTraits>().empty())
    << "the end of a match is not a trigger";

    suite->hearBeacon(suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    suite->reportDelivered<CrashRecordTraits>(frames[0]);

    ShootoutManager* shootoutManager = suite->gameSession->getContext().shootoutManager;
    shootoutManager->startProposal();
    ASSERT_TRUE(shootoutManager->active());
    suite->hearBeacon(suite->fdnMac);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "a record was sent during a shootout";

    shootoutManager->resetToIdle();
    ASSERT_FALSE(shootoutManager->active());
    suite->hearBeacon(suite->fdnMac);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);
    EXPECT_EQ(suite->crashSource.mark, 0u);
}

inline void recordRelayCancelsASendInFlightWhenAMatchStarts(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;

    suite->hearBeacon(suite->fdnMac);
    ASSERT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);

    // Before the device loops at all: the transport's retry pass runs ahead of
    // the session's tick, so a cancel left to the tick would come one retry late.
    suite->startMatch();
    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    suite->pdnDevice.loop();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the frame was retried after the match started";
    // Past every retry the transport would have made of that frame.
    for (int tick = 0; tick < 8; tick++) {
        suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
        suite->pdnDevice.loop();
    }
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the frame was retried during the match";
    EXPECT_EQ(suite->crashSource.mark, 0u);

    // Still pending: it goes out again once the match is over.
    suite->endMatch();
    suite->pdnDevice.loop();
    suite->hearBeacon(suite->fdnMac);
    const std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 1u);
}

inline void recordRelayCancelsASendInFlightWhenAShootoutStarts(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;

    suite->hearBeacon(suite->fdnMac);
    ASSERT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);

    suite->gameSession->getContext().shootoutManager->startProposal();
    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    suite->pdnDevice.loop();

    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the frame was retried during the shootout";
    EXPECT_EQ(suite->crashSource.mark, 0u);

    // A device can also enter a shootout phase other than the first with a
    // frame in flight: here, an abort from idle.
    ShootoutManager* shootoutManager = suite->gameSession->getContext().shootoutManager;
    shootoutManager->resetToIdle();
    suite->hearBeacon(suite->fdnMac);
    ASSERT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);
    shootoutManager->abortTournament();
    ASSERT_EQ(shootoutManager->getPhase(), ShootoutManager::Phase::ABORTED);
    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    suite->pdnDevice.loop();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u)
    << "the frame was retried after the abort";
}

// A match the opponent starts arrives in a packet handler, between two ticks.
inline void
recordRelayCancelsASendInFlightWhenAnInboundCommandStartsAMatch(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;
    suite->player->setIsHunter(false);
    suite->pdnDevice.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK,
                                                            suite->opponentMac);

    suite->hearBeacon(suite->fdnMac);
    ASSERT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);

    MatchPacket command{};
    IdGenerator::copyId(command.matchId, "0f0f0f0f-0000-4000-8000-00000000000a");
    command.isHunter = 1;
    command.command = MatchCmd::SEND_MATCH_ID;
    command.seqId = 1;
    ASSERT_TRUE(suite->pdnDevice.mockPeerComms->triggerPacketCallback(
    suite->opponentMac, PktType::kMatchCommand, reinterpret_cast<const uint8_t*>(&command),
    sizeof(command)));
    ASSERT_TRUE(suite->gameSession->getContext().matchManager->getCurrentMatch().has_value())
    << "the inbound command did not start a match";

    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    suite->pdnDevice.loop();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the frame was retried after the match started";
    EXPECT_EQ(suite->crashSource.mark, 0u);
}

inline void recordRelayDeliversToAnotherFdnWhilePluggedIntoOne(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;

    suite->connectPeer(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN, suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].toMac, relayMacArray(suite->fdnMac));
    // Delivered, but the Connected FDN never acknowledges: full, or busy.
    suite->reportDelivered<CrashRecordTraits>(frames[0]);

    suite->hearBeacon(suite->otherFdnMac);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->otherFdnMac));
    EXPECT_EQ(frames[1].recordNumber, 1u);
}

inline void
recordRelayAdvancesTheMarkOnAnAcknowledgementMidMatchAndSendsNothing(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;

    suite->hearBeacon(suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);

    suite->startMatch();
    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[0].frame));
    EXPECT_EQ(suite->crashSource.mark, 1u);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the next record was sent during the match";

    suite->endMatch();
    suite->pdnDevice.loop();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the next record waits for a trigger";

    suite->hearBeacon(suite->fdnMac);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 2u);
}

// ---- Against a real FDN record store ----

inline void recordRelayDeliversTwoCrashRecordsToAnFdnItIsNotConnectedTo(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;
    suite->bringUpFdn();
    ASSERT_EQ(
    suite->pdnDevice.fakeRemoteDeviceCoordinator.getJackStatus(SerialIdentifier::OUTPUT_JACK),
    JackStatus::DISCONNECTED);
    ASSERT_EQ(
    suite->pdnDevice.fakeRemoteDeviceCoordinator.getJackStatus(SerialIdentifier::INPUT_JACK),
    JackStatus::DISCONNECTED);

    Player fdnPlayer;
    char fdnPlayerId[] = "0000";
    fdnPlayer.setUserID(fdnPlayerId);
    RemotePlayerManager fdnBeacon(suite->fdnDevice.mockPeerComms, DeviceType::FDN);
    fdnBeacon.startBroadcastingPlayerInfo(&fdnPlayer, RECORD_RELAY_BEACON_INTERVAL_MS);
    suite->pumpAir();

    EXPECT_EQ(suite->crashSource.mark, 2u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u);
    const std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u) << "record 1 followed by record 2, each sent once";
    EXPECT_EQ(frames[0].recordNumber, 1u);
    EXPECT_EQ(frames[1].recordNumber, 2u);
    EXPECT_EQ(frames[0].toMac, relayMacArray(suite->fdnMac));
    EXPECT_EQ(frames[1].toMac, relayMacArray(suite->fdnMac));

    // The next beacon finds nothing left to send.
    suite->fakeClock->advance(RECORD_RELAY_BEACON_INTERVAL_MS + 1);
    fdnBeacon.update();
    suite->pumpAir();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u);
}

inline void recordRelayKeepsARecordPendingWhenTheFdnIsFull(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 1;
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
    suite->pumpAir();

    suite->hearBeacon(suite->fdnMac);
    suite->pumpAir();

    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u);
    EXPECT_EQ(suite->crashSource.mark, 0u) << "a record the FDN could not store was retired";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), FDN_CRASH_RECORD_CAPACITY);

    // Delivered and unacknowledged, so it is offered again on the next beacon.
    suite->hearBeacon(suite->fdnMac);
    suite->pumpAir();
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 2u);
    EXPECT_EQ(suite->crashSource.mark, 0u);
}

// As when the write of the mark fails: the record is still the lowest pending
// after its acknowledgement, and sending it straight back would have the FDN
// acknowledge it again without end.
inline void recordRelayWaitsForATriggerWhenTheMarkDoesNotMove(RecordRelayTests* suite) {
    suite->crashSource.newestNumber = 2;
    suite->crashSource.markWritesFail = true;

    suite->hearBeacon(suite->fdnMac);
    std::vector<SentRecordFrame<CrashRecordFramePacket>> frames =
    suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 1u);
    suite->reportDelivered<CrashRecordTraits>(frames[0]);
    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[0].frame));

    EXPECT_EQ(suite->crashSource.advanceCalls, 1u);
    EXPECT_EQ(suite->crashSource.mark, 0u);
    EXPECT_EQ(suite->framesOf<CrashRecordTraits>().size(), 1u)
    << "the acknowledged record was sent straight back";

    // The next beacon offers it again, and a mark that now moves releases the next record.
    suite->crashSource.markWritesFail = false;
    suite->hearBeacon(suite->fdnMac);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].recordNumber, 1u);
    suite->deliverAcknowledgement<CrashRecordTraits>(
    suite->fdnMac, RecordRelayTests::acknowledgementFor<CrashRecordTraits>(frames[1].frame));
    EXPECT_EQ(suite->crashSource.mark, 1u);
    frames = suite->framesOf<CrashRecordTraits>();
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[2].recordNumber, 2u);
}

// ---- Both kinds ----

// The relay is built directly here on a match manager whose storage the test
// seeds, beside a fake crash source. The caller owns the clock, so a failed
// assertion returning from here cannot leave it dangling.
inline void recordRelayRelaysEachKindOnItsOwn() {
    {
        MockDevice device;
        uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
        uint8_t fdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF1};
        ON_CALL(*device.mockPeerComms, getPeerCommsState())
        .WillByDefault(testing::Return(PeerCommsState::ENABLED));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(pdnMac));
        RecordRelayRadioLog radio;
        radio.attach(*device.mockPeerComms);

        // Matches 12 and 13 of 13 are stored and still to be collected. The
        // storage is declared first: the match manager closes it on the way out.
        NativePrefsDriver matchPrefs("relay-kinds-prefs", {MATCHES_PREFS_NAMESPACE});
        matchPrefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, 13);
        matchPrefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 11);
        for (uint32_t number = 12; number <= 13; number++) {
            char matchId[37];
            snprintf(matchId, sizeof(matchId), "%08u-b0b0-4000-8000-000000000000", number);
            const Match match(matchId);
            uint8_t recordBytes[MATCH_BINARY_SIZE] = {};
            match.serialize(recordBytes);
            char key[16];
            snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX,
                     static_cast<unsigned>((number - 1) % MATCH_RING_CAPACITY));
            matchPrefs.writeBytes(MATCHES_PREFS_NAMESPACE, key, recordBytes, sizeof(recordBytes));
        }

        Player player;
        MatchManager matchManager(&player, &matchPrefs, device.wirelessManager,
                                  &device.fakeRemoteDeviceCoordinator);
        ShootoutManager shootoutManager(&player, device.wirelessManager,
                                        &device.fakeRemoteDeviceCoordinator, &matchManager);
        FakeRecordSource<CrashRecordTraits> crashSource;
        crashSource.newestNumber = 1;
        {
            RecordRelay recordRelay(device.wirelessManager, &device.fakeRemoteDeviceCoordinator,
                                    &matchManager, &shootoutManager);
            recordRelay.setCrashRecordSource(&crashSource);

            PlayerInfoPkt beacon{};
            beacon.senderDeviceType = static_cast<uint8_t>(DeviceType::FDN);
            device.mockPeerComms->triggerPacketCallback(fdnMac, PktType::kPlayerInfoBroadcast,
                                                        reinterpret_cast<const uint8_t*>(&beacon),
                                                        sizeof(beacon));

            EXPECT_EQ(radio.framesOf<CrashRecordFramePacket>(PktType::kCrashRecord).size(), 1u);
            std::vector<SentRecordFrame<MatchRecordFramePacket>> matchFrames =
            radio.framesOf<MatchRecordFramePacket>(PktType::kMatchRecord);
            ASSERT_EQ(matchFrames.size(), 1u);
            ASSERT_EQ(matchFrames[0].length, 135u) << "a match record goes out as one frame";
            EXPECT_EQ(matchFrames[0].kind, 0x02);
            EXPECT_EQ(matchFrames[0].recordNumber, 12u);
            EXPECT_EQ(matchFrames[0].toMac, relayMacArray(fdnMac));

            // Only the match record is acknowledged.
            RecordAckPacket ack{};
            ack.seqId = 1;
            ack.kind = matchFrames[0].frame.kind;
            ack.recordNumber = matchFrames[0].frame.recordNumber;
            computeRecordCheck(matchFrames[0].frame.recordBytes,
                               sizeof(matchFrames[0].frame.recordBytes),
                               matchFrames[0].frame.trailer, ack.recordCheck);
            device.mockPeerComms->triggerPacketCallback(
            fdnMac, PktType::kMatchRecordAck, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

            EXPECT_EQ(matchManager.getCollectedHighWaterMark(), 12u);
            matchFrames = radio.framesOf<MatchRecordFramePacket>(PktType::kMatchRecord);
            ASSERT_EQ(matchFrames.size(), 2u);
            EXPECT_EQ(matchFrames[1].recordNumber, 13u);
            EXPECT_EQ(crashSource.mark, 0u) << "the crash record stays pending";
            EXPECT_EQ(radio.framesOf<CrashRecordFramePacket>(PktType::kCrashRecord).size(), 1u);
        }
        // The relay is gone: its beacon handler went with it.
        PlayerInfoPkt beacon{};
        beacon.senderDeviceType = static_cast<uint8_t>(DeviceType::FDN);
        EXPECT_FALSE(device.mockPeerComms->triggerPacketCallback(
        fdnMac, PktType::kPlayerInfoBroadcast, reinterpret_cast<const uint8_t*>(&beacon),
        sizeof(beacon)));
    }
}

inline void recordRelayDoesNotLetOneKindHoldBackTheOther() {
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    clock.setTime(1000);
    recordRelayRelaysEachKindOnItsOwn();
    SimpleTimer::setPlatformClock(nullptr);
}

// A destroyed relay must be out of the match-started and shootout-phase lists
// of the managers it observed. A second relay is built at the first one's
// address on other managers, so an observer the first left behind would reach
// a live relay and cancel its frame, which the radio then shows. Only EXPECT
// is used: the relays live in this function's storage and must be destroyed
// by hand before it returns.
inline void recordRelayUnregistersItsPlayObservers(FakePlatformClock& clock) {
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t fdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF1};
    uint8_t opponentMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    MockDevice firstDevice;
    MockDevice secondDevice;
    ON_CALL(*secondDevice.mockPeerComms, getPeerCommsState())
    .WillByDefault(testing::Return(PeerCommsState::ENABLED));
    ON_CALL(*firstDevice.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(pdnMac));
    ON_CALL(*secondDevice.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(pdnMac));
    RecordRelayRadioLog secondRadio;
    secondRadio.attach(*secondDevice.mockPeerComms);

    Player player;
    MatchManager firstMatchManager(&player, firstDevice.getStorage(), firstDevice.wirelessManager,
                                   &firstDevice.fakeRemoteDeviceCoordinator);
    ShootoutManager firstShootoutManager(&player, firstDevice.wirelessManager,
                                         &firstDevice.fakeRemoteDeviceCoordinator,
                                         &firstMatchManager);
    MatchManager secondMatchManager(&player, secondDevice.getStorage(),
                                    secondDevice.wirelessManager,
                                    &secondDevice.fakeRemoteDeviceCoordinator);
    ShootoutManager secondShootoutManager(&player, secondDevice.wirelessManager,
                                          &secondDevice.fakeRemoteDeviceCoordinator,
                                          &secondMatchManager);
    FakeRecordSource<CrashRecordTraits> crashSource;
    crashSource.newestNumber = 1;

    alignas(RecordRelay) unsigned char relayStorage[sizeof(RecordRelay)];
    RecordRelay* firstRelay = new (relayStorage)
    RecordRelay(firstDevice.wirelessManager, &firstDevice.fakeRemoteDeviceCoordinator,
                &firstMatchManager, &firstShootoutManager);
    firstRelay->~RecordRelay();
    RecordRelay* secondRelay = new (relayStorage)
    RecordRelay(secondDevice.wirelessManager, &secondDevice.fakeRemoteDeviceCoordinator,
                &secondMatchManager, &secondShootoutManager);
    secondRelay->setCrashRecordSource(&crashSource);

    PlayerInfoPkt beacon{};
    beacon.senderDeviceType = static_cast<uint8_t>(DeviceType::FDN);
    secondDevice.mockPeerComms->triggerPacketCallback(fdnMac, PktType::kPlayerInfoBroadcast,
                                                      reinterpret_cast<const uint8_t*>(&beacon),
                                                      sizeof(beacon));
    EXPECT_EQ(secondRadio.framesOf<CrashRecordFramePacket>(PktType::kCrashRecord).size(), 1u);

    // Play on the first relay's managers is nothing to the second relay: its
    // frame is still retried.
    firstMatchManager.initializeShootoutMatch("SHT-first", opponentMac, true);
    clock.advance(Resender::backoffMs(0) + 1);
    secondDevice.wirelessManager->sync();
    EXPECT_EQ(secondRadio.framesOf<CrashRecordFramePacket>(PktType::kCrashRecord).size(), 2u)
    << "the destroyed relay is still told when a match starts";

    firstShootoutManager.startProposal();
    clock.advance(Resender::backoffMs(1) + 1);
    secondDevice.wirelessManager->sync();
    EXPECT_EQ(secondRadio.framesOf<CrashRecordFramePacket>(PktType::kCrashRecord).size(), 3u)
    << "the destroyed relay is still told when a shootout phase is entered";

    secondRelay->~RecordRelay();
}

inline void recordRelayLeavesNoPlayObserverBehind() {
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    clock.setTime(1000);
    recordRelayUnregistersItsPlayObservers(clock);
    SimpleTimer::setPlatformClock(nullptr);
}

// ---- Pending range, mark and search, for any record kind ----

inline void recordPendingRangeStartsAfterTheMarkAndInsideTheRing() {
    struct Row {
        uint32_t mark;
        uint32_t newest;
        uint32_t capacity;
        uint32_t firstPending;
    };
    const uint32_t largest = std::numeric_limits<uint32_t>::max();
    const Row rows[] = {
    // Nothing pending: no record, every record sent, a mark above the newest.
    {0, 0, 10, 0},
    {2, 2, 10, 0},
    {5, 3, 10, 0},
    {0, 0, 32, 0},
    {40, 40, 32, 0},
    // Inside the ring the range starts after the mark.
    {0, 2, 10, 1},
    {1, 2, 10, 2},
    {0, 9, 10, 1},
    {0, 10, 10, 1},
    {20, 25, 10, 21},
    {24, 25, 10, 25},
    {0, 32, 32, 1},
    {31, 32, 32, 32},
    {50, 64, 32, 51},
    // Past it, at the oldest record still held: newest - (capacity - 1).
    {0, 11, 10, 2},
    {3, 25, 10, 16},
    {15, 25, 10, 16},
    {0, 33, 32, 2},
    {3, 64, 32, 33},
    {32, 64, 32, 33},
    {0, largest, 10, largest - 9},
    {0, largest, 32, largest - 31},
    {largest - 1, largest, 10, largest},
    };
    for (const Row& row : rows) {
        EXPECT_EQ(firstPendingRecordNumber(row.mark, row.newest, row.capacity), row.firstPending)
        << "mark " << row.mark << ", newest " << row.newest << ", capacity " << row.capacity;
    }
}

inline void recordMarkMovesOnlyForwardsAndNeverPastTheNewestRecord() {
    struct Row {
        uint32_t mark;
        uint32_t newest;
        uint32_t acknowledged;
        uint32_t markAfter;
    };
    const uint32_t largest = std::numeric_limits<uint32_t>::max();
    const Row rows[] = {
    {0, 2, 1, 1},
    {1, 2, 2, 2},
    // The mark takes the acknowledged number, records passed over included.
    {0, 12, 3, 3},
    {40, 64, 64, 64},
    {largest - 1, largest, largest, largest},
    // Never backwards.
    {2, 3, 1, 2},
    {2, 3, 2, 2},
    {2, 3, 0, 2},
    // Never past the newest record.
    {1, 2, 5, 1},
    {0, 0, 1, 0},
    };
    for (const Row& row : rows) {
        EXPECT_EQ(advancedRecordMark(row.mark, row.newest, row.acknowledged), row.markAfter)
        << "mark " << row.mark << ", newest " << row.newest << ", acknowledged "
        << row.acknowledged;
    }
}

/**
 * A ring in which the listed record numbers cannot be read. It keeps what a
 * search asked to load and what it reported, and, as a source does, what was
 * already reported between searches.
 */
struct FakeRecordRing {
    /** Searches for the lowest readable record above @p mark. */
    bool find(uint32_t mark, uint32_t newest, uint32_t capacity, uint32_t& recordNumber) {
        return findLowestReadableRecord(
        mark, newest, capacity, highestReported, recordNumber,
        [this](uint32_t number) {
            loads.push_back(number);
            return std::find(unreadable.begin(), unreadable.end(), number) == unreadable.end();
        },
        [this](uint32_t number) { reports.push_back(number); });
    }

    std::vector<uint32_t> unreadable;
    std::vector<uint32_t> loads;
    std::vector<uint32_t> reports;
    uint32_t highestReported = 0;
};

inline void recordSearchOffersTheNextReadableRecordAfterAnUnreadableOne() {
    FakeRecordRing ring;
    ring.unreadable = {4, 5};
    uint32_t recordNumber = 0;

    ASSERT_TRUE(ring.find(3, 8, 10, recordNumber));
    EXPECT_EQ(recordNumber, 6u);
    EXPECT_EQ(ring.loads, (std::vector<uint32_t>{4, 5, 6}));
    EXPECT_EQ(ring.reports, (std::vector<uint32_t>{4, 5}));

    // Passing a record over retires nothing: the same search starts from the
    // same record and gives the same answer, and reports nothing twice.
    ring.loads.clear();
    recordNumber = 0;
    ASSERT_TRUE(ring.find(3, 8, 10, recordNumber));
    EXPECT_EQ(recordNumber, 6u);
    EXPECT_EQ(ring.loads, (std::vector<uint32_t>{4, 5, 6})) << "a passed-over record was retired";
    EXPECT_EQ(ring.reports, (std::vector<uint32_t>{4, 5}));

    // Records the ring no longer holds are not asked for at all.
    ring.loads.clear();
    ASSERT_TRUE(ring.find(0, 40, 32, recordNumber));
    EXPECT_EQ(recordNumber, 9u);
    EXPECT_EQ(ring.loads, (std::vector<uint32_t>{9}));
}

inline void recordSearchOffersNothingOnAnUnreadableTail() {
    FakeRecordRing ring;
    ring.unreadable = {4, 5};
    uint32_t recordNumber = 77;

    EXPECT_FALSE(ring.find(3, 5, 10, recordNumber));
    EXPECT_EQ(recordNumber, 77u) << "the output was written with nothing to offer";
    EXPECT_EQ(ring.reports, (std::vector<uint32_t>{4, 5}));

    // Every trigger searches again; each unreadable record is reported once.
    EXPECT_FALSE(ring.find(3, 5, 10, recordNumber));
    EXPECT_FALSE(ring.find(3, 5, 10, recordNumber));
    EXPECT_EQ(ring.reports, (std::vector<uint32_t>{4, 5}));

    // A later record that can be read is offered past the unreadable ones.
    ASSERT_TRUE(ring.find(3, 6, 10, recordNumber));
    EXPECT_EQ(recordNumber, 6u);
    EXPECT_EQ(ring.reports, (std::vector<uint32_t>{4, 5}));

    EXPECT_FALSE(ring.find(6, 6, 10, recordNumber)) << "nothing pending";
}

inline void recordSearchEndsAtTheLargestRecordNumber() {
    const uint32_t largest = std::numeric_limits<uint32_t>::max();
    FakeRecordRing ring;
    ring.unreadable = {largest - 1, largest};
    uint32_t recordNumber = 0;

    EXPECT_FALSE(ring.find(largest - 2, largest, 10, recordNumber));
    EXPECT_EQ(ring.loads, (std::vector<uint32_t>{largest - 1, largest}));

    ring.unreadable = {largest - 1};
    ASSERT_TRUE(ring.find(largest - 2, largest, 10, recordNumber));
    EXPECT_EQ(recordNumber, largest);
}

// ---- Crash record layout ----

inline void crashPacketCarriesTheRecordInItsFixedLayout() {
    CrashRecord record{};
    record.crashNumber = 7;
    record.timestamp = 12500;
    record.resetReason = 4;
    strncpy(record.commitHash, "abcdef12", COMMIT_HASH_LENGTH - 1);
    CoreDumpDetails& details = record.coreDumpDetails.emplace();
    details.programCounter = 0x42001234;
    details.exceptionCause = 28;
    strncpy(details.taskName, "loopTask", TASK_NAME_LENGTH - 1);

    const CrashPacket full = toCrashPacket(record);
    EXPECT_EQ(sizeof(full), 42u);
    EXPECT_EQ(static_cast<uint32_t>(full.crashNumber), 7u);
    EXPECT_EQ(static_cast<uint32_t>(full.timestamp), 12500u);
    EXPECT_EQ(full.resetReason, 4);
    EXPECT_EQ(static_cast<uint32_t>(full.programCounter), 0x42001234u);
    EXPECT_EQ(static_cast<uint32_t>(full.exceptionCause), 28u);
    EXPECT_STREQ(full.taskName, "loopTask");
    EXPECT_STREQ(full.commitHash, "abcdef12");

    // Details without a cause, as after a task watchdog reset.
    record.coreDumpDetails->exceptionCause.reset();
    const CrashPacket noCause = toCrashPacket(record);
    EXPECT_EQ(static_cast<uint32_t>(noCause.exceptionCause), CRASH_PACKET_NO_EXCEPTION_CAUSE);
    EXPECT_EQ(static_cast<uint32_t>(noCause.programCounter), 0x42001234u);
    EXPECT_STREQ(noCause.taskName, "loopTask");

    // No details at all: an empty task name says so.
    record.coreDumpDetails.reset();
    const CrashPacket noDetails = toCrashPacket(record);
    EXPECT_EQ(noDetails.taskName[0], '\0');
    EXPECT_EQ(static_cast<uint32_t>(noDetails.programCounter), 0u);
    EXPECT_EQ(static_cast<uint32_t>(noDetails.exceptionCause), 0u);
    EXPECT_EQ(static_cast<uint32_t>(noDetails.crashNumber), 7u);
    EXPECT_STREQ(noDetails.commitHash, "abcdef12");
}
