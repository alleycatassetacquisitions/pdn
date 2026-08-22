#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "wireless/reliable-channel.hpp"
#include "device/wireless-manager.hpp"
#include "device-mock.hpp"
#include "utility-tests.hpp"

// Probe subclass exposing protected nextSeqId for testing.
// Provides a no-op deliverBytes so the (otherwise pure-virtual) base
// becomes instantiable.
class ProbeChannel : public ReliableChannelBase {
public:
    using ReliableChannelBase::isDuplicateReliableRx;
    using ReliableChannelBase::nextSeqId;
    using ReliableChannelBase::ReliableChannelBase;
    /// No-op body so the otherwise pure-virtual base becomes instantiable.
    bool deliverBytes(const uint8_t*, const uint8_t*, size_t) override { return false; }
    /// Untyped probe: no payload struct, so report 0.
    size_t payloadSize() const override { return 0; }
    /// No-op: this probe exercises only the base's seqId/dedup helpers.
    void onSendResult(const uint8_t*, const uint8_t*, size_t, bool) override {}
};

TEST(ReliableChannelBaseTest, nextSeqIdWrapsAfter255) {
    Resender resender(nullptr);
    ProbeChannel ch(nullptr, &resender, PktType::kChainGameEvent,
                    [](uint8_t, const uint8_t*) {});
    for (int i = 1; i <= 255; ++i) {
        ASSERT_EQ(ch.nextSeqId(), static_cast<uint8_t>(i));
    }
    // The 256th call wraps back to 1 (zero is reserved for "no ack expected").
    ASSERT_EQ(ch.nextSeqId(), uint8_t{1});
}

TEST(ReliableChannelBaseTest, rxDedupEvictsOldestSenderWhenFull) {
    // The per-channel RX dedup cursor table is capped (kMaxRxSenders=32). Senders
    // come and go across a session, so when a 33rd distinct sender arrives the
    // oldest cursor is evicted to keep the table bounded. A wrongly-evicted
    // still-active sender just re-seeds on its next packet (one tolerated
    // re-dispatch); a tracked sender keeps deduping.
    Resender resender(nullptr);
    ProbeChannel ch(nullptr, &resender, PktType::kChainGameEvent,
                    [](uint8_t, const uint8_t*) {});

    auto sender = [](uint8_t i) {
        return std::array<uint8_t, 6>{0x10, 0x20, 0x30, 0x40, 0x50, i};
    };
    const uint8_t seqId = 7;

    // Fill to the cap: each sender's first packet is fresh, not a duplicate.
    for (uint8_t i = 1; i <= 32; ++i) {
        std::array<uint8_t, 6> m = sender(i);
        EXPECT_FALSE(ch.isDuplicateReliableRx(m.data(), seqId));
    }
    // The oldest (sender 1) is still tracked: a repeat is deduped.
    std::array<uint8_t, 6> first = sender(1);
    EXPECT_TRUE(ch.isDuplicateReliableRx(first.data(), seqId));

    // A 33rd distinct sender overflows the table and evicts the oldest cursor.
    std::array<uint8_t, 6> overflow = sender(33);
    EXPECT_FALSE(ch.isDuplicateReliableRx(overflow.data(), seqId));

    // Sender 1's cursor was evicted, so the same packet now reads as fresh.
    EXPECT_FALSE(ch.isDuplicateReliableRx(first.data(), seqId));

    // A sender that was never evicted still dedupes its repeat.
    std::array<uint8_t, 6> stillTracked = sender(32);
    EXPECT_TRUE(ch.isDuplicateReliableRx(stillTracked.data(), seqId));
}

TEST(ResenderTest, distinctSeqIdsToSameTargetCoexist) {
    // A batch of distinct reliable packets to one peer on one channel (e.g. one
    // BRACKET_ENTRY per bracket slot) must each retain an independent retry
    // slot keyed by seqId; sharing (type, target) must not collapse them into
    // one, or a dropped non-final slot would never retransmit.
    Resender resender(nullptr);  // null wm: transmit() no-ops, retry bookkeeping intact
    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    uint8_t payload[4] = {0};
    const Resender::SendMode kStream = Resender::SendMode::KEEP_DISTINCT;
    for (uint8_t seq = 1; seq <= 3; ++seq) {
        resender.send(target, PktType::kShootoutCommand, seq,
                      payload, sizeof(payload), kStream);
    }
    EXPECT_EQ(resender.pendingCount(PktType::kShootoutCommand), 3u);

    // Acking the middle seqId clears only that slot.
    EXPECT_TRUE(resender.onAck(PktType::kShootoutCommand, 2, target));
    EXPECT_EQ(resender.pendingCount(PktType::kShootoutCommand), 2u);

    // A genuine re-send of an existing seqId replaces rather than duplicates.
    resender.send(target, PktType::kShootoutCommand, 1,
                  payload, sizeof(payload), kStream);
    EXPECT_EQ(resender.pendingCount(PktType::kShootoutCommand), 2u);

    // cancel() drops every remaining slot to the target on that channel.
    resender.cancel(PktType::kShootoutCommand, target);
    EXPECT_EQ(resender.pendingCount(PktType::kShootoutCommand), 0u);
}

TEST(ResenderTest, supersedeDropsPriorAndStaleAckDoesNotResurrect) {
    // The whole point of SupersedePerTarget (DRAW_RESULT / NEVER_PRESSED): a
    // newer send to the same peer obsoletes the prior unacked one, so only the
    // latest is armed. A late ack for the superseded seqId must match nothing and
    // must not resurrect it or disturb the surviving entry; otherwise a stale
    // retransmit could land after the newer state.
    Resender resender(nullptr);  // null wm: transmit() no-ops, retry bookkeeping intact
    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    uint8_t payload[4] = {0};
    const Resender::SendMode kState = Resender::SendMode::SUPERSEDE_PER_TARGET;

    // Send A, then supersede with B before A is acked.
    resender.send(target, PktType::kQuickdrawCommand, /*seqA=*/5,
                  payload, sizeof(payload), kState);
    resender.send(target, PktType::kQuickdrawCommand, /*seqB=*/6,
                  payload, sizeof(payload), kState);
    // A was dropped on supersede; only B remains.
    EXPECT_EQ(resender.pendingCount(PktType::kQuickdrawCommand), 1u);

    // A stale ack for the superseded A matches nothing and leaves B armed.
    EXPECT_FALSE(resender.onAck(PktType::kQuickdrawCommand, /*seqA=*/5, target));
    EXPECT_EQ(resender.pendingCount(PktType::kQuickdrawCommand), 1u);

    // The surviving entry is B (not a resurrected A): acking B clears it.
    EXPECT_TRUE(resender.onAck(PktType::kQuickdrawCommand, /*seqB=*/6, target));
    EXPECT_EQ(resender.pendingCount(PktType::kQuickdrawCommand), 0u);
}

// ---- Broadcast fan-out ----
//
// The fan-out exists because the ESP-NOW peer table holds 20 entries, so a ring
// larger than that cannot be addressed by unicast at all. These cases pin the
// contract that makes one frame safe to treat as N reliable deliveries.

namespace {
/// Fan-out harness: a mocked radio whose frames are counted, a fake clock, and
/// a Resender wired to both. Counts frames rather than trusting bookkeeping,
/// since "one frame, N pending" is the whole claim.
struct BroadcastFixture {
    ::testing::NiceMock<MockPeerComms> comms;
    WirelessManager wm{&comms, nullptr};
    FakePlatformClock clock;
    Resender resender{&wm};
    int frames = 0;

    /** Radio up and every frame counted; the fake clock drives retry rounds. */
    BroadcastFixture() {
        SimpleTimer::setPlatformClock(&clock);
        setRadioUp(true);
    }
    /** Releases the platform clock this fixture installed. */
    ~BroadcastFixture() { SimpleTimer::setPlatformClock(nullptr); }

    /** A distinct member MAC; only the last byte varies. */
    static std::array<uint8_t, 6> mac(uint8_t last) {
        return {0x02, 0, 0, 0, 0, last};
    }
    /** One retry round. Advances past any backoff a member could be sitting on,
     *  so a sync() is exactly one round for every member no matter how far each
     *  has progressed — counting rounds is how these cases read a retry budget,
     *  which is not otherwise observable. */
    void round() {
        clock.advance(Resender::backoffMs(Resender::MAX_RETRIES) + 1);
        resender.sync();
    }

    /** Radio up or down. Down means sendEspNowData reports the frame never left. */
    void setRadioUp(bool up) {
        ON_CALL(comms, sendData(::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillByDefault([this, up](const uint8_t*, PktType, const uint8_t*, const size_t) {
                frames++;
                return up ? 1 : -1;
            });
    }
};
}  // namespace

TEST(ResenderBroadcastTest, oneFramePerRoundNotOnePerMember) {
    // The reason the fan-out is broadcast at all. Four members owe an ack, and a
    // retransmit round must put ONE frame on the wire, not four — a per-member
    // unicast retry is what exhausts the 20-slot peer table.
    BroadcastFixture f;
    std::vector<std::array<uint8_t, 6>> members = {
        f.mac(1), f.mac(2), f.mac(3), f.mac(4)};
    uint8_t payload[8] = {0};

    f.resender.sendBroadcast(members, PktType::kShootoutCommand, 7, payload, sizeof(payload));
    EXPECT_EQ(f.frames, 1);
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 4u);

    f.round();
    EXPECT_EQ(f.frames, 2) << "a retransmit round must be one frame, not one per member";
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 4u);
}

TEST(ResenderBroadcastTest, memberAckClearsOnlyItsOwnSlot) {
    // Per-member accounting is what makes a broadcast a reliable delivery rather
    // than a hope. One member acking must not clear the group.
    BroadcastFixture f;
    std::vector<std::array<uint8_t, 6>> members = {f.mac(1), f.mac(2), f.mac(3)};
    uint8_t payload[4] = {0};

    f.resender.sendBroadcast(members, PktType::kShootoutCommand, 9, payload, sizeof(payload));
    std::array<uint8_t, 6> second = f.mac(2);

    EXPECT_TRUE(f.resender.onAck(PktType::kShootoutCommand, 9, second.data()));
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 2u);
    EXPECT_FALSE(f.resender.isPending(PktType::kShootoutCommand, second.data()));

    std::array<uint8_t, 6> first = f.mac(1);
    EXPECT_TRUE(f.resender.isPending(PktType::kShootoutCommand, first.data()));

    // A second ack from the same member matches nothing and disturbs no one.
    EXPECT_FALSE(f.resender.onAck(PktType::kShootoutCommand, 9, second.data()));
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 2u);
}

TEST(ResenderBroadcastTest, silentMemberAbandonsAloneAndNamesItself) {
    // The case a fan-out exists to detect: one member never acks. It must burn
    // its own budget, abandon by name so the caller can act on that member, and
    // leave the members that did ack untouched.
    BroadcastFixture f;
    std::vector<std::array<uint8_t, 6>> members = {f.mac(1), f.mac(2)};
    uint8_t payload[4] = {0};

    std::vector<std::array<uint8_t, 6>> abandoned;
    f.resender.setAbandonCallback(
        [&abandoned](PktType, uint8_t, const uint8_t* target, const uint8_t*, size_t) {
            std::array<uint8_t, 6> mac{};
            memcpy(mac.data(), target, 6);
            abandoned.push_back(mac);
        });

    f.resender.sendBroadcast(members, PktType::kShootoutCommand, 3, payload, sizeof(payload));
    std::array<uint8_t, 6> acker = f.mac(1);
    ASSERT_TRUE(f.resender.onAck(PktType::kShootoutCommand, 3, acker.data()));

    // Silent member burns MAX_RETRIES rounds, then abandons on the round after.
    for (uint8_t retry = 0; retry <= Resender::MAX_RETRIES; ++retry) {
        f.round();
    }

    ASSERT_EQ(abandoned.size(), 1u);
    EXPECT_EQ(memcmp(abandoned[0].data(), f.mac(2).data(), 6), 0)
        << "abandon must name the member that went silent, not the group";
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 0u);
}

TEST(ResenderBroadcastTest, failedRadioSendCostsNoRetryAndIsAttemptedOnce) {
    // A send that never reaches the radio must leave every member's budget
    // untouched, and must be ONE attempt for the group — not one attempt per
    // member all failing in the same tick.
    BroadcastFixture f;
    std::vector<std::array<uint8_t, 6>> members = {
        f.mac(1), f.mac(2), f.mac(3), f.mac(4), f.mac(5)};
    uint8_t payload[4] = {0};

    int abandons = 0;
    f.resender.setAbandonCallback(
        [&abandons](PktType, uint8_t, const uint8_t*, const uint8_t*, size_t) { abandons++; });

    f.resender.sendBroadcast(members, PktType::kShootoutCommand, 5, payload, sizeof(payload));
    ASSERT_EQ(f.frames, 1);

    // Radio unavailable for exactly one round.
    f.setRadioUp(false);
    f.round();
    EXPECT_EQ(f.frames, 2) << "one failed attempt for the group, not one per member";
    f.setRadioUp(true);

    // The budget is read by counting rounds to abandonment. The failed round
    // must not have shortened it, so a full MAX_RETRIES of real retransmits
    // still has to happen before anyone is given up on.
    for (uint8_t retry = 0; retry < Resender::MAX_RETRIES; ++retry) {
        f.round();
    }
    EXPECT_EQ(abandons, 0)
        << "a frame that never reached the radio must not cost a member a retry";
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 5u);

    // The round after the budget is spent abandons every member, once each.
    f.round();
    EXPECT_EQ(abandons, 5);
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 0u);
}

TEST(ResenderBroadcastTest, cancelDropsOneMemberAndResendReplacesTheGroup) {
    // cancel() is the unreachable-peer path and must not take the rest of the
    // ring down with it. A re-send of the same seqId replaces the group outright
    // rather than re-arming members that already acked.
    BroadcastFixture f;
    std::vector<std::array<uint8_t, 6>> members = {f.mac(1), f.mac(2), f.mac(3)};
    uint8_t payload[4] = {0};

    f.resender.sendBroadcast(members, PktType::kShootoutCommand, 4, payload, sizeof(payload));
    std::array<uint8_t, 6> gone = f.mac(3);
    f.resender.cancel(PktType::kShootoutCommand, gone.data());
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 2u);
    EXPECT_FALSE(f.resender.isPending(PktType::kShootoutCommand, gone.data()));

    std::array<uint8_t, 6> acked = f.mac(1);
    ASSERT_TRUE(f.resender.onAck(PktType::kShootoutCommand, 4, acked.data()));
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 1u);

    // Same seqId again: the group is rebuilt from the members named now.
    f.resender.sendBroadcast(members, PktType::kShootoutCommand, 4, payload, sizeof(payload));
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 3u);
}

TEST(ResenderBroadcastTest, emptyMemberListSendsNothing) {
    // A fan-out with nobody to hear it is not a delivery. Sending anyway would
    // put a frame on the air that no ack can ever clear, and the group would sit
    // pending until it abandoned against nobody.
    BroadcastFixture f;
    uint8_t payload[4] = {0};
    f.resender.sendBroadcast({}, PktType::kShootoutCommand, 2, payload, sizeof(payload));
    EXPECT_EQ(f.frames, 0);
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 0u);
}
