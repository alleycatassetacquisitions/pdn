#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "utils/simple-timer.hpp"
#include "device/drivers/peer-comms-types.hpp"

class WirelessManager;

// Reliable unicast: send a packet to a peer, retransmit on timeout, abandon
// after maxRetries. Per-target pending granularity depends on SendMode: state
// channels keep one entry per (PktType, target); stream channels keep one per
// (PktType, target, seqId) so a batch of distinct packets to the same peer all
// retain their own retry slots. sync() must be called every loop tick to drive
// retransmits.

class Resender {
public:
    // How a send relates to other in-flight sends to the same peer on the same
    // channel. SUPERSEDE_PER_TARGET (default): the payload is current state, so a
    // newer send obsoletes any prior unacked one and only the latest survives
    // (an older retransmit arriving last would otherwise reinstate stale state).
    // KEEP_DISTINCT: the payload is one item of a stream (e.g. a bracket slot),
    // so each seqId keeps its own retry slot and a dropped one still retransmits.
    enum class SendMode { SUPERSEDE_PER_TARGET,
                          KEEP_DISTINCT };

    // Retry tuning, shared by every channel: first retransmit after 100ms,
    // doubling each retry, capped at 3 retries.
    static constexpr unsigned long INITIAL_TIMEOUT_MS = 100;
    static constexpr uint8_t MAX_RETRIES = 3;

    /// Exponential backoff for the given retry number: 100, 200, 400 ...
    static constexpr unsigned long backoffMs(uint8_t retryNum) {
        // Clamp the shift so raising MAX_RETRIES past ~25 can't hit shift UB
        // (unsigned long is 32-bit on the ESP32).
        return INITIAL_TIMEOUT_MS << (retryNum > 16 ? 16u : retryNum);
    }

    /// Fires once per pending entry that exhausts its retry budget. Invoked
    /// from sync() AFTER all retransmits have been processed and the
    /// abandoned entries removed, so callbacks may freely call send() or
    /// cancel() on this Resender without invalidating the iteration.
    using AbandonCallback = std::function<void(PktType type, uint8_t seqId,
                                               const uint8_t* targetMac)>;

    /// wirelessManager may be nullptr in unit tests; transmit() then no-ops.
    explicit Resender(WirelessManager* wirelessManager)
        : wirelessManager(wirelessManager) {}
    /// Pending entries own their payload copies; nothing external to release.
    ~Resender() = default;

    /// Registers the once-per-abandoned-entry callback (see AbandonCallback).
    void setAbandonCallback(AbandonCallback cb) {
        abandonCallback = std::move(cb);
    }

    /// Reliable send. SendMode controls how it relates to other pending sends to
    /// the same (type, target): SUPERSEDE_PER_TARGET drops any prior one,
    /// KEEP_DISTINCT keeps prior sends with a different seqId. payload bytes are
    /// copied.
    void send(const uint8_t* target, PktType type, uint8_t seqId,
              const uint8_t* payload, size_t len,
              SendMode mode = SendMode::SUPERSEDE_PER_TARGET);

    /// Clears the matching pending entry. Returns true when one matched.
    /// Driven by the radio SEND_SUCCESS callback (the peer's MAC ack), which
    /// replaces the old application-level ACK round-trip. A SEND_FAIL is
    /// deliberately ignored: the existing backoff timer retransmits on timeout,
    /// which avoids burning the whole retry budget on a briefly-absent peer.
    /// (SEND_FAIL as a fast-retry accelerant is left as a follow-up; its
    /// interaction with backoff is unsettled.)
    bool onAck(PktType type, uint8_t seqId, const uint8_t* fromMac);

    /// Silent drop; use when the target is known unreachable. No abandon callback.
    void cancel(PktType type, const uint8_t* target);

    /// Drives retransmits and abandonment. Must be called every loop tick.
    void sync();

    /// Number of pending entries on this channel, across all targets.
    size_t pendingCount(PktType type) const {
        size_t count = 0;
        for (const Pending& p : pending) {
            if (p.type == type) ++count;
        }
        return count;
    }

    /// True when at least one entry to this target is pending on this channel.
    bool isPending(PktType type, const uint8_t* target) const {
        if (target == nullptr) return false;
        for (const Pending& p : pending) {
            if (p.type != type) continue;
            if (memcmp(p.target.data(), target, 6) == 0) return true;
        }
        return false;
    }

private:
    struct Pending {
        PktType type;
        std::array<uint8_t, 6> target;
        uint8_t seqId;
        std::vector<uint8_t> payload;
        uint8_t retries;
        SimpleTimer timer;
    };

    std::vector<Pending>::iterator findPending(
        PktType type, uint8_t seqId, const uint8_t* target);

    // Drop every pending entry to `target` on this PktType, across all seqIds.
    void eraseAllToTarget(PktType type, const uint8_t* target);

    // Returns false when the frame never reached the radio, so the caller can
    // avoid spending a retry on a packet that was not actually sent.
    bool transmit(const Pending& p);

    WirelessManager* wirelessManager;
    std::vector<Pending> pending;
    AbandonCallback abandonCallback;
};
