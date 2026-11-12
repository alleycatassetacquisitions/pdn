#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "wireless/reliable-channel.hpp"
#include "wireless/reliable-transport.hpp"
#include "device/wireless-manager.hpp"
#include "device-mock.hpp"

// Generic wire payload for exercising the transport. The reliable layer
// requires a settable `seqId`; `cmd` is opaque payload data. Defined here so
// these tests stay independent of any game-specific payload type.
struct TransportTestPayload {
    uint8_t cmd;
    uint8_t seqId;
    uint8_t data[14];
} __attribute__((packed));

namespace {
// Every frame the mock radio was handed, in order.
struct RadioTap {
    struct Frame {
        std::array<uint8_t, 6> dst;
        PktType type;
        std::vector<uint8_t> bytes;
    };
    std::vector<Frame> frames;

    /** Records every frame the mock radio is handed; sendResult is what sendData reports. */
    void attach(MockPeerComms& comms, int sendResult = 1) {
        using ::testing::_;
        ON_CALL(comms, sendData(_, _, _, _))
        .WillByDefault([this, sendResult](const uint8_t* dst, PktType type, const uint8_t* data,
                                          const size_t len) {
            Frame f;
            std::memcpy(f.dst.data(), dst, 6);
            f.type = type;
            f.bytes.assign(data, data + len);
            frames.push_back(std::move(f));
            return sendResult;
        });
    }
    /** Frames recorded so far on one PktType. */
    size_t count(PktType type) const {
        size_t n = 0;
        for (const Frame& f : frames) {
            if (f.type == type) n++;
        }
        return n;
    }
};
}  // namespace

TEST(ReliableTransportTest, seqIdReachesTheBytesHandedToTheRadio) {
    // Every other test here feeds back an echo it stamped itself, which passes
    // whether or not the id reached the wire. This one reads the bytes the radio
    // was given: a stamp at the wrong offset is a silent wire bug, because both
    // the receiver's dedup and the sender's ack recover identity from that byte.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    RadioTap radio;
    radio.attach(mockComms);
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload payload{};
    const uint8_t seq = ch->sendReliable(target, payload);

    ASSERT_NE(seq, 0);
    ASSERT_EQ(radio.frames.size(), 1u);
    ASSERT_EQ(radio.frames[0].bytes.size(), sizeof(TransportTestPayload));
    EXPECT_EQ(radio.frames[0].bytes[offsetof(TransportTestPayload, seqId)], seq)
    << "the seqId the channel returned is not in the bytes the radio was given";
    EXPECT_EQ(payload.seqId, seq) << "the id did not reach the caller's struct";
    delete ch;
}

TEST(ReliableTransportTest, aSecondClaimOfAPktTypeIsRefused) {
    // Two owners of one PktType on one device is a wiring bug; the first keeps
    // the type and the second claim returns nothing.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    ReliableChannel<TransportTestPayload>* first =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(transport.channel<TransportTestPayload>(PktType::kChainGameEvent), nullptr);

    uint8_t from[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    int deliveries = 0;
    first->onReceive([&](const uint8_t*, const TransportTestPayload&) { deliveries++; });
    EXPECT_TRUE(transport.deliverIncoming(PktType::kChainGameEvent, from,
                                          reinterpret_cast<const uint8_t*>(&p), sizeof(p)));
    EXPECT_EQ(deliveries, 1);
    delete first;
}

TEST(ReliableTransportTest, aDestroyedChannelLeavesItsTypeUnclaimed) {
    // The transport routes abandons by PktType to whatever channel holds the
    // slot; a channel that has gone must not still be reachable.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    uint8_t from[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    EXPECT_TRUE(transport.deliverIncoming(PktType::kChainGameEvent, from,
                                          reinterpret_cast<const uint8_t*>(&p), sizeof(p)));
    delete ch;
    EXPECT_FALSE(transport.deliverIncoming(PktType::kChainGameEvent, from,
                                           reinterpret_cast<const uint8_t*>(&p), sizeof(p)));
    ReliableChannel<TransportTestPayload>* again =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    EXPECT_NE(again, nullptr);
    delete again;
}

TEST(ReliableTransportTest, deliverIncomingReturnsFalseWhenChannelMissing) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    uint8_t from[6] = {1, 2, 3, 4, 5, 6};
    uint8_t data[4] = {0};
    EXPECT_FALSE(transport.deliverIncoming(PktType::kChainGameEvent, from, data, sizeof(data)));
}

TEST(ReliableTransportTest, sendReliableTriggersAck) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    bool abandoned = false;
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    ch->setOnAbandon([&](uint8_t, const uint8_t*) { abandoned = true; });

    bool received = false;
    TransportTestPayload deliveredP{};
    ch->onReceive([&](const uint8_t* /*mac*/, const TransportTestPayload& p) {
        received = true;
        deliveredP = p;
    });

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    p.cmd = 7;
    uint8_t seq = ch->sendReliable(target, p);
    ASSERT_NE(seq, 0);
    ASSERT_TRUE(ch->isPending(target));

    // Radio SEND_SUCCESS for the packet we sent clears the pending entry. The
    // channel reads the stamped seqId back out of the echoed payload bytes.
    TransportTestPayload sentEcho{};
    sentEcho.seqId = seq;
    transport.onSendResult(PktType::kChainGameEvent, target,
                           reinterpret_cast<const uint8_t*>(&sentEcho), sizeof(sentEcho), true);
    ASSERT_FALSE(ch->isPending(target));
    ASSERT_FALSE(abandoned);

    // Inbound packet: deliverIncoming -> channel->deliver -> onReceive fires.
    TransportTestPayload incoming{};
    incoming.cmd = 99;
    incoming.seqId = 42;
    transport.deliverIncoming(PktType::kChainGameEvent, target,
                              reinterpret_cast<const uint8_t*>(&incoming), sizeof(incoming));
    ASSERT_TRUE(received);
    ASSERT_EQ(deliveredP.cmd, 99);
    delete ch;
}

TEST(ReliableTransportTest, sendFailLeavesPendingForBackoffRetry) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    uint8_t seq = ch->sendReliable(target, p);
    ASSERT_TRUE(ch->isPending(target));

    // A SEND_FAIL must NOT clear the pending entry: loss is left to the backoff
    // timer, so the entry stays armed for retransmit.
    TransportTestPayload sentEcho{};
    sentEcho.seqId = seq;
    transport.onSendResult(PktType::kChainGameEvent, target,
                           reinterpret_cast<const uint8_t*>(&sentEcho), sizeof(sentEcho), false);
    EXPECT_TRUE(ch->isPending(target));
    delete ch;
}

TEST(ReliableTransportTest, sendResultSeqIdZeroDoesNotAck) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    ch->sendReliable(target, p);  // stamps a nonzero seqId
    ASSERT_TRUE(ch->isPending(target));

    // A fire-and-forget send-result (seqId 0) has no sender-side pending entry
    // to match, so it must not clear the live reliable pending entry.
    TransportTestPayload fireAndForget{};
    fireAndForget.seqId = 0;
    transport.onSendResult(PktType::kChainGameEvent, target,
                           reinterpret_cast<const uint8_t*>(&fireAndForget), sizeof(fireAndForget),
                           true);
    EXPECT_TRUE(ch->isPending(target));
    delete ch;
}

TEST(ReliableTransportTest, abandonWhenSpanRunsOut) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int abandonCount = 0;
    uint8_t abandonedSeqId = 0;
    std::array<uint8_t, 6> abandonedMac{};
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    ch->setOnAbandon([&](uint8_t seqId, const uint8_t* mac) {
        abandonCount++;
        abandonedSeqId = seqId;
        std::memcpy(abandonedMac.data(), mac, 6);
    });

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    p.cmd = 7;
    uint8_t seq = ch->sendReliable(target, p);

    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    for (int i = 0; i < 8; ++i) {
        clock.advance(1000);
        transport.sync();
    }
    SimpleTimer::setPlatformClock(nullptr);

    ASSERT_EQ(abandonCount, 1);
    ASSERT_EQ(abandonedSeqId, seq);
    ASSERT_EQ(std::memcmp(abandonedMac.data(), target, 6), 0);
    ASSERT_FALSE(ch->isPending(target));
    delete ch;
}

TEST(ReliableTransportTest, aSendTheRadioNeverTookIsAbandonedOnTime) {
    // sendData < 0 is the local path refusing the frame, and two of its causes are
    // permanent for that frame (a payload over the packet size, a radio held in the
    // wrong mode) rather than transient. The entry is bounded by elapsed time, so
    // the caller is told once and the channel stops holding the target pending —
    // callers gate on isPending and would otherwise wait out the session.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int abandonCount = 0;
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    ch->setOnAbandon([&](uint8_t, const uint8_t*) { abandonCount++; });

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _)).WillRepeatedly(Return(-1));

    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    ch->sendReliable(target, p);

    // Still inside the span: the frame keeps being offered to the path.
    for (unsigned long elapsed = 0; elapsed < Resender::RETRANSMIT_SPAN_MS - 100;
         elapsed += 20) {
        clock.advance(20);
        transport.sync();
    }
    EXPECT_EQ(abandonCount, 0);
    EXPECT_TRUE(ch->isPending(target));

    for (int i = 0; i < 10; ++i) {
        clock.advance(20);
        transport.sync();
    }
    EXPECT_EQ(abandonCount, 1);
    EXPECT_FALSE(ch->isPending(target));
    SimpleTimer::setPlatformClock(nullptr);
    delete ch;
}

TEST(ReliableTransportTest, ackRoutesByPktType) {
    // Each channel allocates seqIds independently, so two channels can have
    // pending sends to the same target with identical seqIds. An ack routed to
    // one channel's PktType must clear that channel's entry, not the other's;
    // otherwise the unacked channel retries to abandon and tears down state
    // via its abandon callback.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    ReliableChannel<TransportTestPayload>* chA =
    transport.channel<TransportTestPayload>(PktType::kShootoutCommand);
    ReliableChannel<TransportTestPayload>* chB =
    transport.channel<TransportTestPayload>(PktType::kMatchCommand);

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    // Send B first so its pending entry sits ahead of A's in the Resender vector.
    uint8_t seqB = chB->sendReliable(target, p);
    uint8_t seqA = 0;
    do {
        seqA = chA->sendReliable(target, p);
    } while (seqA != seqB);
    ASSERT_EQ(seqA, seqB);
    ASSERT_TRUE(chA->isPending(target));
    ASSERT_TRUE(chB->isPending(target));

    TransportTestPayload sentEcho{};
    sentEcho.seqId = seqA;
    transport.onSendResult(PktType::kShootoutCommand, target,
                           reinterpret_cast<const uint8_t*>(&sentEcho), sizeof(sentEcho), true);

    ASSERT_FALSE(chA->isPending(target));
    ASSERT_TRUE(chB->isPending(target));
    delete chA;
    delete chB;
}

TEST(ReliableTransportTest, droppedNonFinalSlotStillRetransmits) {
    // End-to-end: a coordinator sends three distinct reliable packets to one
    // peer on one channel, the peer acks the later two but the first is lost.
    // The first must remain armed and ultimately abandon, not be silently
    // dropped because two later sends share its (type, target).
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int abandonCount = 0;
    uint8_t abandonedSeqId = 0;
    ReliableChannel<TransportTestPayload>* ch = transport.channel<TransportTestPayload>(
    PktType::kShootoutCommand, ReliableChannelBase::OnAbandon{}, Resender::SendMode::KEEP_DISTINCT);
    ch->setOnAbandon([&](uint8_t seqId, const uint8_t*) {
        abandonCount++;
        abandonedSeqId = seqId;
    });

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    uint8_t s1 = ch->sendReliable(target, p);  // this one will be "lost"
    uint8_t s2 = ch->sendReliable(target, p);
    uint8_t s3 = ch->sendReliable(target, p);
    ASSERT_NE(s1, s2);
    ASSERT_NE(s2, s3);

    // Peer acks the two later slots; s1 stays pending.
    for (uint8_t seq : {s2, s3}) {
        TransportTestPayload sentEcho{};
        sentEcho.seqId = seq;
        transport.onSendResult(PktType::kShootoutCommand, target,
                               reinterpret_cast<const uint8_t*>(&sentEcho), sizeof(sentEcho), true);
    }
    ASSERT_TRUE(ch->isPending(target));  // s1 survived the later sends

    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    for (int i = 0; i < 8; ++i) {
        clock.advance(1000);
        transport.sync();
    }
    SimpleTimer::setPlatformClock(nullptr);

    EXPECT_EQ(abandonCount, 1);
    EXPECT_EQ(abandonedSeqId, s1);
    EXPECT_FALSE(ch->isPending(target));
    delete ch;
}

TEST(ReliableTransportTest, duplicateReliableDeliveryDispatchesOnce) {
    // A lost SEND_SUCCESS makes the sender resend, so the receiver sees the same
    // (sender, seqId) twice; a unicast channel dispatches onReceive only once.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int deliveries = 0;
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    ch->onReceive([&](const uint8_t*, const TransportTestPayload&) { deliveries++; });

    uint8_t from[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    p.seqId = 7;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&p);
    transport.deliverIncoming(PktType::kChainGameEvent, from, bytes, sizeof(p));
    transport.deliverIncoming(PktType::kChainGameEvent, from, bytes, sizeof(p));
    EXPECT_EQ(deliveries, 1);

    // A new seqId from the same sender is a fresh message and dispatches.
    p.seqId = 8;
    transport.deliverIncoming(PktType::kChainGameEvent, from, bytes, sizeof(p));
    EXPECT_EQ(deliveries, 2);

    // A different sender with a colliding seqId must not be suppressed.
    uint8_t from2[6] = {9, 9, 9, 9, 9, 9};
    p.seqId = 7;
    transport.deliverIncoming(PktType::kChainGameEvent, from2, bytes, sizeof(p));
    EXPECT_EQ(deliveries, 3);
    delete ch;
}

TEST(ReliableTransportTest, unsequencedDeliveryNeverDeduped) {
    // seqId==0 is the unsequenced sentinel: those payloads dedup by domain
    // identity at the caller (e.g. forwarded CONFIRMs all carry 0), so the base
    // must dispatch every one.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int deliveries = 0;
    ReliableChannel<TransportTestPayload>* ch =
    transport.channel<TransportTestPayload>(PktType::kChainGameEvent);
    ch->onReceive([&](const uint8_t*, const TransportTestPayload&) { deliveries++; });

    uint8_t from[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    p.seqId = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&p);
    transport.deliverIncoming(PktType::kChainGameEvent, from, bytes, sizeof(p));
    transport.deliverIncoming(PktType::kChainGameEvent, from, bytes, sizeof(p));
    EXPECT_EQ(deliveries, 2);
    delete ch;
}

TEST(ReliableTransportTest, destroyingAChannelStopsItsPendingSends) {
    ::testing::NiceMock<MockPeerComms> comms;
    WirelessManager wm{&comms, nullptr};
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    Resender resender(&wm);

    int frames = 0;
    ON_CALL(comms, getPeerCommsState()).WillByDefault(::testing::Return(PeerCommsState::ENABLED));
    ON_CALL(comms, sendData(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillByDefault([&frames](const uint8_t*, PktType, const uint8_t*, const size_t) {
            frames++;
            return 1;
        });

    const uint8_t peer[6] = {0x02, 0, 0, 0, 0, 0x09};
    {
        ReliableChannel<TransportTestPayload> channel(
            &wm, &resender, PktType::kShootoutCommand, nullptr);
        TransportTestPayload p{};
        channel.sendReliable(peer, p);
        ASSERT_EQ(frames, 1);
        ASSERT_EQ(resender.pendingCount(PktType::kShootoutCommand), 1u);
    }

    EXPECT_EQ(resender.pendingCount(PktType::kShootoutCommand), 0u);
    clock.advance(Resender::INITIAL_TIMEOUT_MS + 1);
    resender.sync();
    EXPECT_EQ(frames, 1) << "a destroyed channel kept retransmitting";

    SimpleTimer::setPlatformClock(nullptr);
}

TEST(ReliableTransportTest, backgroundEspNowSendDoesNotPreemptWifiMode) {
    ::testing::NiceMock<MockPeerComms> comms;
    ::testing::NiceMock<MockHttpClient> http;
    WirelessManager wm{&comms, &http};

    PeerCommsState commsState = PeerCommsState::ENABLED;
    HttpClientState httpState = HttpClientState::DISCONNECTED;
    ON_CALL(comms, getPeerCommsState()).WillByDefault(::testing::Invoke([&]() {
        return commsState;
    }));
    ON_CALL(comms, setPeerCommsState(::testing::_))
    .WillByDefault(::testing::Invoke([&](PeerCommsState s) { commsState = s; }));
    ON_CALL(http, getHttpClientState()).WillByDefault(::testing::Invoke([&]() {
        return httpState;
    }));
    ON_CALL(http, setHttpClientState(::testing::_))
    .WillByDefault(::testing::Invoke([&](HttpClientState s) { httpState = s; }));

    wm.enableWifiMode();
    ASSERT_EQ(wm.getCurrentMode(), WirelessMode::WIFI);
    ASSERT_EQ(httpState, HttpClientState::CONNECTED);
    ASSERT_EQ(commsState, PeerCommsState::DISABLED);

    const uint8_t dst[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t payload[4] = {1, 2, 3, 4};
    EXPECT_EQ(wm.sendEspNowData(dst, PktType::kSymbolMatchCommand, payload, sizeof(payload)), -1);
    EXPECT_EQ(wm.getCurrentMode(), WirelessMode::WIFI);
    EXPECT_EQ(httpState, HttpClientState::CONNECTED);
}
