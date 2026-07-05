#pragma once

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include "wireless/reliable-channel.hpp"

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
