#pragma once

#include <array>
#include <cstddef>
#include <cstring>
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

TEST(ReliableTransportTest, reclaimSamePayloadReturnsSameChannel) {
    // A re-claim of a PktType with the same payload type (e.g. a re-created
    // owner re-initializing) hands back the existing channel rather than
    // duplicating or crashing.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    ReliableChannel<TransportTestPayload>* first = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent, [](uint8_t, const uint8_t*) {});
    ASSERT_NE(first, nullptr);
    ReliableChannel<TransportTestPayload>* again = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent, [](uint8_t, const uint8_t*) {});
    EXPECT_EQ(again, first);
}

TEST(ReliableTransportTest, reclaimDifferentPayloadOnSameTypeRejected) {
    // Two subsystems claiming one PktType with different payloads is a wiring
    // bug; the second claim is rejected (nullptr) rather than clobbering.
    struct WiderPayload {
        uint8_t cmd;
        uint8_t seqId;
        uint8_t data[40];
    } __attribute__((packed));

    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    ReliableChannel<TransportTestPayload>* first = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent, [](uint8_t, const uint8_t*) {});
    ASSERT_NE(first, nullptr);
    ReliableChannel<WiderPayload>* collision = transport.channel<WiderPayload>(
        PktType::kChainGameEvent, [](uint8_t, const uint8_t*) {});
    EXPECT_EQ(collision, nullptr);
}

TEST(ReliableTransportTest, deliverIncomingReturnsFalseWhenChannelMissing) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);
    uint8_t from[6] = {1, 2, 3, 4, 5, 6};
    uint8_t data[4] = {0};
    EXPECT_FALSE(transport.deliverIncoming(
        PktType::kChainGameEvent, from, data, sizeof(data)));
}

TEST(ReliableTransportTest, sendReliableTriggersAck) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    bool abandoned = false;
    ReliableChannel<TransportTestPayload>* ch = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent,
        [&](uint8_t, const uint8_t*) { abandoned = true; });

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

    AckPayload ack{static_cast<uint8_t>(PktType::kChainGameEvent), seq};
    transport.onAckPacket(target, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    ASSERT_FALSE(ch->isPending(target));
    ASSERT_FALSE(abandoned);

    // Inbound packet: deliverIncoming -> channel.deliver -> onReceive fires.
    TransportTestPayload incoming{};
    incoming.cmd = 99;
    incoming.seqId = 42;
    transport.deliverIncoming(PktType::kChainGameEvent,
                              target,
                              reinterpret_cast<const uint8_t*>(&incoming),
                              sizeof(incoming));
    ASSERT_TRUE(received);
    ASSERT_EQ(deliveredP.cmd, 99);
}

TEST(ReliableTransportTest, abandonAfterMaxRetries) {
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int abandonCount = 0;
    uint8_t abandonedSeqId = 0;
    std::array<uint8_t, 6> abandonedMac{};
    ReliableChannel<TransportTestPayload>* ch = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent,
        [&](uint8_t seqId, const uint8_t* mac) {
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
}

TEST(ReliableTransportTest, failedSendDoesNotConsumeRetry) {
    // A send that never reaches the radio (sendData < 0: transient PSRAM
    // pressure, or ESP-NOW not ready mid mode-switch) must not spend a retry.
    // The packet stays pending and keeps retrying rather than being abandoned
    // against a budget it never actually used.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int abandonCount = 0;
    ReliableChannel<TransportTestPayload>* ch = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent,
        [&](uint8_t, const uint8_t*) { abandonCount++; });

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _)).WillRepeatedly(Return(-1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    ch->sendReliable(target, p);

    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    // Far more cycles than maxRetries: with the bug this abandons; fixed, it
    // keeps retrying because nothing ever transmitted.
    for (int i = 0; i < 20; ++i) {
        clock.advance(1000);
        transport.sync();
    }
    EXPECT_EQ(abandonCount, 0);
    EXPECT_TRUE(ch->isPending(target));

    // Once sends actually reach the radio (still unacked), normal abandonment
    // resumes, proving the no-abandon above is the send-failure path, not a
    // dead retry loop.
    ::testing::Mock::VerifyAndClearExpectations(&mockComms);
    EXPECT_CALL(mockComms, sendData(_, _, _, _)).WillRepeatedly(Return(0));
    for (int i = 0; i < 20; ++i) {
        clock.advance(1000);
        transport.sync();
    }
    SimpleTimer::setPlatformClock(nullptr);
    EXPECT_EQ(abandonCount, 1);
    EXPECT_FALSE(ch->isPending(target));
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

    ReliableChannel<TransportTestPayload>* chA = transport.channel<TransportTestPayload>(
        PktType::kShootoutCommand,
        [](uint8_t, const uint8_t*) {});
    ReliableChannel<TransportTestPayload>* chB = transport.channel<TransportTestPayload>(
        PktType::kQuickdrawCommand,
        [](uint8_t, const uint8_t*) {});

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    // Send B first so its pending entry sits ahead of A's in the Resender vector.
    uint8_t seqB = chB->sendReliable(target, p);
    uint8_t seqA = chA->sendReliable(target, p);
    ASSERT_EQ(seqA, seqB);
    ASSERT_TRUE(chA->isPending(target));
    ASSERT_TRUE(chB->isPending(target));

    AckPayload ack{static_cast<uint8_t>(PktType::kShootoutCommand), seqA};
    transport.onAckPacket(target, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

    ASSERT_FALSE(chA->isPending(target));
    ASSERT_TRUE(chB->isPending(target));
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
        PktType::kShootoutCommand,
        [&](uint8_t seqId, const uint8_t*) {
            abandonCount++;
            abandonedSeqId = seqId;
        },
        Resender::SendMode::KEEP_DISTINCT);  // stream channel, like BRACKET_ENTRY

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
        AckPayload ack{static_cast<uint8_t>(PktType::kShootoutCommand), seq};
        transport.onAckPacket(target, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
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
}

TEST(ReliableTransportTest, duplicateReliableDeliveryDispatchesOnce) {
    // A lost ack makes the sender resend, so the receiver sees the same
    // (sender, seqId) twice. The reliable layer must ack both (to silence the
    // sender) yet dispatch onReceive only once.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int deliveries = 0;
    ReliableChannel<TransportTestPayload>* ch = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent,
        [](uint8_t, const uint8_t*) {});
    ch->onReceive([&](const uint8_t*, const TransportTestPayload&) { deliveries++; });

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

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
}

TEST(ReliableTransportTest, unsequencedDeliveryNeverDeduped) {
    // seqId==0 is the sendOnce/unsequenced sentinel: those payloads dedup by
    // domain identity at the caller (e.g. forwarded CONFIRMs all carry 0), so
    // the base must dispatch every one.
    ::testing::NiceMock<MockPeerComms> mockComms;
    WirelessManager wm(&mockComms, nullptr);
    ReliableTransport transport(&wm);

    int deliveries = 0;
    ReliableChannel<TransportTestPayload>* ch = transport.channel<TransportTestPayload>(
        PktType::kChainGameEvent,
        [](uint8_t, const uint8_t*) {});
    ch->onReceive([&](const uint8_t*, const TransportTestPayload&) { deliveries++; });

    using ::testing::_;
    using ::testing::Return;
    EXPECT_CALL(mockComms, sendData(_, _, _, _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(1));

    uint8_t from[6] = {1, 2, 3, 4, 5, 6};
    TransportTestPayload p{};
    p.seqId = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&p);
    transport.deliverIncoming(PktType::kChainGameEvent, from, bytes, sizeof(p));
    transport.deliverIncoming(PktType::kChainGameEvent, from, bytes, sizeof(p));
    EXPECT_EQ(deliveries, 2);
}
