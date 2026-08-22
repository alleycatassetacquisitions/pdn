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

    // What a retry costs when the radio refuses the frame. TRANSMITTED_ONLY: a
    // frame that never left does not spend a recipient's budget, so a brief
    // outage costs nothing and the entry keeps retrying — right where nothing
    // downstream is waiting on abandonment, and what the device-layer channels
    // have always done. EVERY_ROUND: a due round spends a retry whether or not
    // the frame left, so a send path that stays shut still reaches abandonment —
    // required where abandonment is the caller's only liveness signal, as it is
    // for a tournament whose next match waits on a fan-out clearing.
    enum class BudgetPolicy { TRANSMITTED_ONLY,
                              EVERY_ROUND };

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
    void sendBroadcast(const std::vector<std::array<uint8_t, 6>>& recipients,
                       PktType type, uint8_t seqId,
                       const uint8_t* payload, size_t len,
                       SendMode mode = SendMode::KEEP_DISTINCT);

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
    // recipient's own MAC for a unicast, the broadcast MAC for a fan-out. It is
    // stored rather than derived from the recipient count, because a fan-out to
    // a single remaining member must stay broadcast: a supporter several cables
    // away is not in the peer table and cannot be addressed directly.
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
    // group left with none. This is what SUPERSEDE_PER_TARGET means for both
    // shapes: whoever the new frame speaks to stops owing anything to the old one.
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
