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
    using ReliableChannelBase::RX_SEQ_CLAIM_MS;
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

TEST(ReliableChannelBaseTest, aRestartedSenderIsNotMistakenForARetransmit) {
    // seqIds start at 1 on a fresh channel, so the first frame a peer sends after
    // rebooting carries the same seqId the receiver already holds from before the
    // reboot. Suppressing that as a duplicate loses the frame — and on a channel
    // that sends one frame per peer, such as the role announce, the cursor is
    // ALWAYS sitting on 1, so it is lost every reboot rather than occasionally.
    // The sender is told nothing: the radio still MAC-acks the frame, so its
    // retry clears and it records the peer as told.
    //
    // What separates the two cases is time, not content. A duplicate is a
    // retransmit, and retransmits stop when the sender's budget runs out.
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    clock.setTime(1000);
    Resender resender(nullptr);
    ProbeChannel ch(nullptr, &resender, PktType::kRoleAnnounce,
                    [](uint8_t, const uint8_t*) {});
    std::array<uint8_t, 6> peer = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

    // First frame of the peer's life.
    EXPECT_FALSE(ch.isDuplicateReliableRx(peer.data(), 1));
    // Its retransmit, inside the retry window: genuinely a repeat.
    clock.advance(300);
    EXPECT_TRUE(ch.isDuplicateReliableRx(peer.data(), 1));

    // The peer reboots and announces again from a fresh counter. Far past any
    // window it could still have been retransmitting in.
    clock.advance(ProbeChannel::RX_SEQ_CLAIM_MS + 1);
    EXPECT_FALSE(ch.isDuplicateReliableRx(peer.data(), 1))
        << "a restarted sender's first frame was swallowed as a duplicate";

    SimpleTimer::setPlatformClock(nullptr);
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
    Resender resender;
    int frames = 0;

    /** Radio up and every frame counted; the fake clock drives retry rounds.
     *  The budget policy is the fixture's subject as often as the fan-out is:
     *  whether a refused frame costs a retry decides whether a caller waiting on
     *  abandonment ever hears anything. */
    explicit BroadcastFixture(
        Resender::BudgetPolicy policy = Resender::BudgetPolicy::EVERY_ROUND)
        : resender(&wm, policy) {
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

TEST(ResenderBroadcastTest, refusedFrameIsOneAttemptForTheGroup) {
    // Whatever the budget policy, a round the radio refuses must be ONE attempt
    // for the whole fan-out, not one per recipient all failing in the same tick.
    BroadcastFixture f;
    std::vector<std::array<uint8_t, 6>> recipients = {
        f.mac(1), f.mac(2), f.mac(3), f.mac(4), f.mac(5)};
    uint8_t payload[4] = {0};

    f.resender.sendBroadcast(recipients, PktType::kShootoutCommand, 5, payload, sizeof(payload));
    ASSERT_EQ(f.frames, 1);

    f.setRadioUp(false);
    f.round();
    EXPECT_EQ(f.frames, 2) << "one refused attempt for the group, not one per recipient";
}

TEST(ResenderBroadcastTest, budgetPolicyDecidesWhetherARefusingRadioEverAbandons) {
    // The whole reason the policy exists. Same outage, same rounds, opposite
    // outcomes: a caller whose next step waits on abandonment must eventually be
    // told, while a caller with nothing downstream must not spend a peer's budget
    // on frames that never left.
    uint8_t payload[4] = {0};

    {
        BroadcastFixture waiting(Resender::BudgetPolicy::EVERY_ROUND);
        int abandons = 0;
        waiting.resender.setAbandonCallback(
            [&abandons](PktType, uint8_t, const uint8_t*, const uint8_t*, size_t) {
                abandons++;
            });
        std::vector<std::array<uint8_t, 6>> recipients = {waiting.mac(1), waiting.mac(2)};
        waiting.resender.sendBroadcast(recipients, PktType::kShootoutCommand, 8,
                                       payload, sizeof(payload));
        waiting.setRadioUp(false);
        for (uint8_t r = 0; r <= Resender::MAX_RETRIES; ++r)
            waiting.round();
        EXPECT_EQ(abandons, 2) << "a refusing radio must end the fan-out, not suspend it";
        EXPECT_EQ(waiting.resender.pendingCount(PktType::kShootoutCommand), 0u);
    }
    {
        BroadcastFixture patient(Resender::BudgetPolicy::TRANSMITTED_ONLY);
        int abandons = 0;
        patient.resender.setAbandonCallback(
            [&abandons](PktType, uint8_t, const uint8_t*, const uint8_t*, size_t) {
                abandons++;
            });
        std::vector<std::array<uint8_t, 6>> recipients = {patient.mac(1), patient.mac(2)};
        patient.resender.sendBroadcast(recipients, PktType::kShootoutCommand, 8,
                                       payload, sizeof(payload));
        patient.setRadioUp(false);
        for (uint8_t r = 0; r <= Resender::MAX_RETRIES + 4; ++r)
            patient.round();
        EXPECT_EQ(abandons, 0) << "a frame that never left must not spend a recipient's budget";
        EXPECT_EQ(patient.resender.pendingCount(PktType::kShootoutCommand), 2u);
    }
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

    // Same seqId again: the group is replaced, rebuilt from whoever is named
    // now — including the member that already acked, since the caller named it.
    f.resender.sendBroadcast(members, PktType::kShootoutCommand, 4, payload, sizeof(payload));
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 3u);
}

TEST(ResenderBroadcastTest, sameSeqIdToADifferentPeerIsADifferentFrame) {
    // One channel can address two peers out of one seqId space — the role announce
    // does exactly that, one jack each. A seqId therefore names a frame only
    // together with where it went, so the replacement scan has to compare the
    // destination too. Matching on (type, seqId) alone lets the second peer's send
    // erase the first peer's still-pending frame, which then never retransmits and
    // never abandons: it is simply gone, and that peer is never told anything.
    BroadcastFixture f;
    uint8_t payload[4] = {0};
    std::array<uint8_t, 6> x = f.mac(1);
    std::array<uint8_t, 6> y = f.mac(2);

    f.resender.send(x.data(), PktType::kRoleAnnounce, 5, payload, sizeof(payload),
                    Resender::SendMode::KEEP_DISTINCT);
    f.resender.send(y.data(), PktType::kRoleAnnounce, 5, payload, sizeof(payload),
                    Resender::SendMode::KEEP_DISTINCT);

    EXPECT_EQ(f.resender.pendingCount(PktType::kRoleAnnounce), 2u);
    EXPECT_TRUE(f.resender.isPending(PktType::kRoleAnnounce, x.data()));
    EXPECT_TRUE(f.resender.isPending(PktType::kRoleAnnounce, y.data()));
}

TEST(ResenderBroadcastTest, emptyMemberListSendsNothing) {
    // A fan-out with nobody to hear it is not a delivery. Sending anyway would put
    // a frame on the air that nobody was asked to answer for.
    BroadcastFixture f;
    uint8_t payload[4] = {0};
    f.resender.sendBroadcast({}, PktType::kShootoutCommand, 2, payload, sizeof(payload));
    EXPECT_EQ(f.frames, 0);
    EXPECT_EQ(f.resender.pendingCount(PktType::kShootoutCommand), 0u);
}
