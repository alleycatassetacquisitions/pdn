#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "utils/simple-timer.hpp"
#include "device/drivers/peer-comms-types.hpp"

class WirelessManager;

// Reliable send: put a frame on the air, retransmit it on a backoff, give up on
// it when its span runs out. One frame can owe delivery to one peer or to many —
// a ring can hold more devices than the ESP-NOW peer table has slots, so a
// fan-out goes out once addressed to the broadcast MAC and is tracked per
// recipient. Both shapes are the same record here: a frame, the address it is
// sent to, and the recipients still expected to answer for it. sync() must run
// every loop tick.

class Resender {
public:
    // How a send relates to other in-flight sends on the same channel that name
    // the same recipient. SUPERSEDE_PER_TARGET (default): the payload is current
    // state, so a newer send obsoletes any prior unacked one and only the latest
    // survives — an older retransmit arriving last would otherwise reinstate
    // stale state. KEEP_DISTINCT: the payload is one item of a stream (a bracket
    // slot, one of several command families sharing a PktType), so each send
    // keeps its own retry slot and a dropped one still retransmits.
    enum class SendMode { SUPERSEDE_PER_TARGET,
                          KEEP_DISTINCT };

    /// Retry tuning, shared by every channel: first retransmit after 100ms, the
    /// gap doubling each round, until the frame's span runs out — three retransmits
    /// at these values. The count is not theirs alone: sync() tests the deadline
    /// before the round, and that order is load-bearing rather than cosmetic. Test
    /// the round first and a loop that stalled past the span emits one more
    /// retransmit on the way out, after the window the receiver claims the seqId
    /// for has been sized shut.
    static constexpr unsigned long INITIAL_TIMEOUT_MS = 100;

    /// Wall-clock span from a frame's first send to its recipients being given up
    /// on. Every group is armed with this as a deadline, so it bounds the frame
    /// whatever the local send path does.
    ///
    /// Both ends read it, which is why it is one stated number: past it the sender
    /// hands the send path no further copy, and the receiver holds a seqId claim
    /// wider than it on that promise (see ReliableChannel::RX_SEQ_CLAIM_MS). Nothing
    /// on the wire carries a deadline, so a per-frame one would drive the claim to
    /// the widest caller's regardless. Widen this and the claim widens with it.
    static constexpr unsigned long RETRANSMIT_SPAN_MS = 1500;

    // Nothing else ties the first backoff to the span, and getting it backwards
    // degrades silently: one send, no retransmit, then abandonment.
    static_assert(INITIAL_TIMEOUT_MS < RETRANSMIT_SPAN_MS,
                  "a frame would be sent once and given up on, never retransmitted");

    /// The soonest a caller may treat a frame as finished with. The span bounds
    /// hand-off, not airtime: transmit() only queues, so a copy handed over just
    /// inside the span can leave the radio after it, and the 500ms covers that
    /// drain. Both the receiver's duplicate-claim window and a sender's repair
    /// cadence are this same question, so they read it here rather than each
    /// re-deriving the arithmetic.
    static constexpr unsigned long STALE_AFTER_MS = RETRANSMIT_SPAN_MS + 500;

    /// Exponential backoff for the given round: 100, 200, 400 ...
    static constexpr unsigned long backoffMs(uint8_t round) {
        return INITIAL_TIMEOUT_MS << round;
    }

    /// Fires once per recipient that is given up on. Invoked from sync() AFTER
    /// every retransmit has been processed and the abandoned recipients removed,
    /// so a callback may freely send(), cancel() or cancelAll() on this Resender
    /// without invalidating the iteration. `payload` is the frame that was given
    /// up on, so a caller multiplexing several command families onto one PktType
    /// can read which one it was straight off the bytes.
    using AbandonCallback = std::function<void(PktType type, uint8_t seqId,
                                               const uint8_t* targetMac,
                                               const uint8_t* payload, size_t payloadLen)>;

    /// wirelessManager may be nullptr in unit tests; transmit() then no-ops.
    explicit Resender(WirelessManager* wirelessManager)
        : wirelessManager(wirelessManager) {}
    /// Groups own their payload copies; nothing external to release.
    ~Resender() = default;

    /// Registers the once-per-abandoned-recipient callback (see AbandonCallback).
    void setAbandonCallback(AbandonCallback cb) {
        abandonCallback = std::move(cb);
    }

    /// Cumulative counters for everything this Resender carries. Sends and
    /// retries count FRAMES — one fan-out retransmit is one retry however many
    /// recipients it covers — while abandons count RECIPIENTS given up on, since
    /// that is the number that names devices rather than airtime. The two do not
    /// divide into one another.
    struct Stats {
        uint32_t sends = 0;
        uint32_t retries = 0;
        uint32_t abandons = 0;
    };
    const Stats& getStats() const { return stats; }

    /// Reliable send to one peer: the frame is addressed to that peer and it is
    /// the only recipient expected to answer. payload bytes are copied.
    void send(const uint8_t* target, PktType type, uint8_t seqId,
              const uint8_t* payload, size_t len,
              SendMode mode = SendMode::SUPERSEDE_PER_TARGET);

    /// Reliable fan-out: ONE frame addressed to the broadcast MAC, with every
    /// named recipient expected to answer for it separately. Each is acked
    /// independently, but the retry schedule belongs to the frame: a round emits a
    /// single frame however many still owe an ack, and the ones still owing when
    /// the span runs out are given up on together.
    ///
    /// Broadcast rather than a unicast per recipient because the ESP-NOW peer
    /// table holds 20 entries, so a ring larger than that cannot be addressed by
    /// unicast at all, while the broadcast slot is registered once at radio init.
    ///
    /// Naming no recipients sends nothing: a frame nobody is expected to answer
    /// for is not a delivery.
    ///
    /// Always KEEP_DISTINCT. Superseding is per-recipient, so on a fan-out it
    /// would retire only the recipients the new frame happens to name, leaving a
    /// member that has since left the ring still owing an ack on the old one. A
    /// caller that wants the previous fan-out gone wants all of it gone, which is
    /// cancelAll.
    void sendBroadcast(const std::vector<std::array<uint8_t, 6>>& recipients,
                       PktType type, uint8_t seqId,
                       const uint8_t* payload, size_t len);

    /// Clears this recipient's obligation for the frame sent under `seqId`.
    /// Returns true when one matched. For a unicast that is the radio's
    /// SEND_SUCCESS (the peer's MAC ack); a fan-out gets no per-recipient radio
    /// evidence, so there it is an application ack. A SEND_FAIL is deliberately
    /// ignored: the driver's own MAC retries take milliseconds, so retransmitting
    /// on one would collapse the backoff into dozens of rounds inside the span.
    bool onAck(PktType type, uint8_t seqId, const uint8_t* fromMac);

    /// Silent drop of one recipient's obligations on this channel; use when the
    /// target is known unreachable. No abandon callback.
    void cancel(PktType type, const uint8_t* target);

    /// Silent drop of every obligation on this channel, to every recipient. For
    /// when the conversation itself is over, not just one peer's part in it.
    void cancelAll(PktType type);

    /// cancelAll, sparing the frame sent under `keepSeqId`. For the frame that
    /// announces the conversation is over: it owes delivery to exactly the peers
    /// that have not yet heard the news, so it must outlive the teardown it is
    /// reporting. 0 spares nothing, so a caller that allocates seqId 0 cannot
    /// spare that frame.
    void cancelAllExcept(PktType type, uint8_t keepSeqId);

    /// Drives retransmits and abandonment. Must be called every loop tick.
    void sync();

    /// Recipients on this channel that still owe an ack, across all frames.
    size_t pendingCount(PktType type) const {
        size_t count = 0;
        for (const Group& g : groups) {
            if (g.type == type) count += g.recipients.size();
        }
        return count;
    }

    /// Recipients of the frame sent under `seqId` that still owe an ack. Zero
    /// once every one of them has answered or been given up on.
    size_t pendingCount(PktType type, uint8_t seqId) const {
        size_t count = 0;
        for (const Group& g : groups) {
            if (g.type == type && g.seqId == seqId) count += g.recipients.size();
        }
        return count;
    }

    /// True when this target still owes an ack on this channel, whether it was
    /// addressed directly or named as one recipient of a fan-out.
    bool isPending(PktType type, const uint8_t* target) const {
        if (target == nullptr) return false;
        for (const Group& g : groups) {
            if (g.type != type) continue;
            for (const std::array<uint8_t, 6>& r : g.recipients) {
                if (memcmp(r.data(), target, 6) == 0) return true;
            }
        }
        return false;
    }

private:
    // One frame in flight. `destination` is the address it goes to — the
    // recipient's own MAC for a unicast, the broadcast MAC for a fan-out — and is
    // part of the frame's identity, since one channel can address several peers
    // out of a single seqId space.
    //
    // Stored rather than derived from the recipient count. Deriving it would let
    // a fan-out quietly turn into a unicast as its members ack away, changing how
    // an already-airborne frame is addressed and spending a peer-table slot per
    // remaining member. The driver does register unicast peers on demand, so the
    // wall is the 20-slot table, not membership in it.
    struct Group {
        PktType type;
        uint8_t seqId;
        std::array<uint8_t, 6> destination;
        std::vector<uint8_t> payload;
        std::vector<std::array<uint8_t, 6>> recipients;
        // The retry schedule is the frame's, not each recipient's: one send arms
        // them all together and nothing joins a live group, so a recipient has
        // nothing of its own to remember but its address.
        SimpleTimer retransmitTimer;
        SimpleTimer deadlineTimer;
        uint8_t round = 0;
    };

    void addGroup(PktType type, uint8_t seqId, const std::array<uint8_t, 6>& destination,
                  const std::vector<std::array<uint8_t, 6>>& recipients,
                  const uint8_t* payload, size_t len, SendMode mode);

    // Drop these recipients from every prior group on this channel, and drop any
    // group left with none. Reached from a SUPERSEDE_PER_TARGET send and from
    // cancel() — and cancel() reaches fan-out groups too, dropping one member
    // out of a live broadcast.
    void supersedeRecipients(PktType type,
                             const std::vector<std::array<uint8_t, 6>>& recipients);

    // False when the local send path would not take the frame. Only the counters
    // and the log read this: it is not evidence the frame reached the air either
    // way, since sendData discards the radio's own result and reports the queue.
    bool transmit(const Group& g);

    struct AbandonedEntry {
        PktType type;
        uint8_t seqId;
        std::array<uint8_t, 6> target;
        std::vector<uint8_t> payload;
    };

    WirelessManager* wirelessManager;
    std::vector<Group> groups;
    AbandonCallback abandonCallback;
    Stats stats;
};
