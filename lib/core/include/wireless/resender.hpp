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
// a budget. One frame can owe delivery to one peer or to many — a ring can hold
// more devices than the ESP-NOW peer table has slots, so a fan-out goes out once
// addressed to the broadcast MAC and is tracked per recipient. Both shapes are
// the same record here: a frame, the address it is sent to, and the recipients
// still expected to answer for it. sync() must run every loop tick.

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

    // Retry tuning, shared by every channel: first retransmit after 100ms,
    // doubling each retry, capped at 3 retries.
    static constexpr unsigned long INITIAL_TIMEOUT_MS = 100;
    static constexpr uint8_t MAX_RETRIES = 3;

    // What a due round costs when the local send path refuses the frame — the
    // only case the two differ. TRANSMITTED_ONLY parks the entry until the path
    // reopens; EVERY_ROUND spends anyway, so a path that stays shut still
    // abandons. See budgetPolicyDecidesWhetherARefusingRadioEverAbandons.
    //
    // ReliableTransport parks (its abandons are no-ops and the RDC re-sends on
    // the next chain-state event). Both game managers abandon: the shootout
    // gates its next match on a fan-out clearing, and the chain duel needs the
    // entry to stop rather than re-attempt at the 100ms floor indefinitely.
    enum class BudgetPolicy { TRANSMITTED_ONLY,
                              EVERY_ROUND };

    /// Wall-clock span from a frame's first send to a recipient being given up
    /// on, so every retransmit of it falls inside. Holds under EVERY_ROUND; a
    /// TRANSMITTED_ONLY entry against a shut send path has no bound at all,
    /// because a refused round costs no budget.
    static constexpr unsigned long retransmitSpanMs() {
        unsigned long total = 0;
        for (uint8_t r = 0; r <= MAX_RETRIES; ++r)
            total += backoffMs(r);
        return total;
    }

    /// The soonest a caller may treat a frame as finished with: past every
    /// retransmit of it, plus margin. Bounded only for an EVERY_ROUND sender —
    /// see retransmitSpanMs; a TRANSMITTED_ONLY entry behind a shut send path
    /// has no bound, so a caller relying on this must tolerate a later copy. Both the receiver's duplicate-claim window
    /// and a sender's repair cadence are this same question, so they read it
    /// here rather than each re-deriving the arithmetic.
    static constexpr unsigned long staleAfterMs() { return retransmitSpanMs() + 500; }

    /// Exponential backoff for the given retry number: 100, 200, 400 ...
    static constexpr unsigned long backoffMs(uint8_t retryNum) {
        // Clamp the shift so raising MAX_RETRIES past ~25 can't hit shift UB
        // (unsigned long is 32-bit on the ESP32).
        return INITIAL_TIMEOUT_MS << (retryNum > 16 ? 16u : retryNum);
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
    explicit Resender(WirelessManager* wirelessManager,
                      BudgetPolicy budgetPolicy = BudgetPolicy::TRANSMITTED_ONLY)
        : wirelessManager(wirelessManager)
        , budgetPolicy(budgetPolicy) {}
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
    /// named recipient expected to answer for it separately. Each carries its own
    /// retry budget and is given up on independently, but a retransmit round
    /// emits a single frame however many still owe an ack.
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
    /// ignored: the backoff timer retransmits on timeout, which avoids burning
    /// the whole budget on a briefly-absent peer.
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
    /// reporting. A caller whose seqId space includes 0 cannot spare a frame
    /// this way.
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
            for (const Recipient& r : g.recipients) {
                if (memcmp(r.target.data(), target, 6) == 0) return true;
            }
        }
        return false;
    }

private:
    // One device expected to answer for a frame. Carries only what differs
    // between recipients; the frame itself lives on the group, so a large
    // fan-out holds one copy rather than one per recipient.
    struct Recipient {
        std::array<uint8_t, 6> target;
        uint8_t retries;
        SimpleTimer timer;
    };

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
        std::vector<Recipient> recipients;
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

    // Returns false when the frame never reached the radio, so the caller can
    // avoid spending a retry on a packet that was not actually sent.
    bool transmit(const Group& g);

    struct AbandonedEntry {
        PktType type;
        uint8_t seqId;
        std::array<uint8_t, 6> target;
        std::vector<uint8_t> payload;
    };

    WirelessManager* wirelessManager;
    BudgetPolicy budgetPolicy;
    std::vector<Group> groups;
    AbandonCallback abandonCallback;
    Stats stats;
};
