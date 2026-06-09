#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

#include "utils/simple-timer.hpp"
#include "device/drivers/peer-comms-types.hpp"

class WirelessManager;

// Reliable unicast: send a packet to a peer, retransmit on timeout, abandon
// after maxRetries. Per-target pending granularity depends on SendMode: state
// channels keep one entry per (PktType, subType, target); stream channels keep
// one per (PktType, subType, target, seqId) so a batch of distinct packets to
// the same peer all retain their own retry slots. sync() must be called every
// loop tick to drive retransmits.

// One retry policy for every channel; Resender::kPolicy is the single
// instance. (Namespace scope because a nested struct's default member
// initializers can't be evaluated inside the enclosing class definition.)
struct ResenderRetryPolicy {
    unsigned long initialTimeoutMs = 100;
    uint8_t maxRetries = 3;

    // Exponential: 100, 200, 400 ...
    constexpr unsigned long backoffMs(uint8_t retryNum) const {
        // unsigned long is 32-bit on the ESP32; an unclamped shift from a
        // caller-set maxRetries would be UB past ~25 doublings.
        unsigned shift = retryNum > 16 ? 16u : retryNum;
        return initialTimeoutMs << shift;
    }

    // Time from the first transmission until the last retransmit leaves the
    // radio; after this window a reliable send can no longer reach the peer
    // (abandonment fires one further backoff later).
    constexpr unsigned long totalBudgetMs() const {
        unsigned long total = 0;
        for (uint8_t i = 0; i < maxRetries; ++i) total += backoffMs(i);
        return total;
    }
};

class Resender {
public:
    // How a send relates to other in-flight sends to the same peer on the same
    // channel. SupersedePerTarget (default): the payload is current state, so a
    // newer send obsoletes any prior unacked one and only the latest survives
    // (an older retransmit arriving last would otherwise reinstate stale state).
    // KeepDistinct: the payload is one item of a stream (e.g. a bracket slot),
    // so each seqId keeps its own retry slot and a dropped one still retransmits.
    enum class SendMode { SupersedePerTarget, KeepDistinct };

    using RetryPolicy = ResenderRetryPolicy;
    static constexpr RetryPolicy kPolicy{};

    struct Stats {
        uint32_t sends = 0;
        uint32_t retries = 0;
        uint32_t abandons = 0;
        uint32_t ackLatencyMsSum = 0;
        uint32_t ackCount = 0;
    };

    // Fires once per pending entry that exhausts its retry budget. Invoked
    // from sync() AFTER all retransmits have been processed and the
    // abandoned entries removed, so callbacks may freely call send() or
    // cancel() on this Resender without invalidating the iteration.
    using AbandonCallback = std::function<void(PktType type, uint8_t subType,
                                               uint8_t seqId,
                                               const uint8_t* targetMac)>;

    explicit Resender(WirelessManager* wirelessManager)
        : wm_(wirelessManager) {}
    ~Resender() = default;

    // Send a kAck for a reliably-received packet. All subsystems share
    // this one ack wire format.
    static void sendAck(WirelessManager* wm, const uint8_t* toMac,
                        PktType originalType, uint8_t subType, uint8_t seqId);

    void setAbandonCallback(AbandonCallback cb) {
        abandonCallback_ = std::move(cb);
    }

    // Reliable send. SendMode controls how it relates to other pending sends to
    // the same (type, subType, target): SupersedePerTarget drops any prior one,
    // KeepDistinct keeps prior sends with a different seqId. payload bytes are
    // copied.
    void send(const uint8_t* target, PktType type, uint8_t subType,
              uint8_t seqId, const uint8_t* payload, size_t len,
              SendMode mode = SendMode::SupersedePerTarget);

    bool onAck(PktType type, uint8_t subType, uint8_t seqId, const uint8_t* fromMac);

    // Silent drop; use when the target is known unreachable. No abandon callback.
    void cancel(PktType type, uint8_t subType, const uint8_t* target);

    // Must be called every loop tick.
    void sync();

    Stats getStats(PktType type, uint8_t subType = 0) const {
        auto it = perChannelStats_.find(channelKey(type, subType));
        if (it == perChannelStats_.end()) return Stats{};
        return it->second;
    }

    size_t pendingCount(PktType type, uint8_t subType = 0) const {
        size_t count = 0;
        for (const auto& p : pending_) {
            if (p.type == type && p.subType == subType) ++count;
        }
        return count;
    }

    bool isPending(PktType type, uint8_t subType, const uint8_t* target) const {
        if (target == nullptr) return false;
        for (const auto& p : pending_) {
            if (p.type != type || p.subType != subType) continue;
            if (memcmp(p.target.data(), target, 6) == 0) return true;
        }
        return false;
    }

private:
    struct Pending {
        PktType type;
        uint8_t subType;
        std::array<uint8_t, 6> target;
        uint8_t seqId;
        std::vector<uint8_t> payload;
        uint8_t retries;
        SimpleTimer timer;
    };

    Stats& statsFor(PktType type, uint8_t subType) {
        return perChannelStats_[channelKey(type, subType)];
    }

    std::vector<Pending>::iterator findPending(
        PktType type, uint8_t subType, uint8_t seqId, const uint8_t* target);

    // Drop every pending entry to `target` on (type, subType), across all seqIds.
    void eraseAllToTarget(PktType type, uint8_t subType, const uint8_t* target);

    // Returns false when the frame never reached the radio, so the caller can
    // avoid spending a retry on a packet that was not actually sent.
    bool transmit(const Pending& p);

    WirelessManager* wm_;
    std::vector<Pending> pending_;
    std::map<ChannelKey, Stats> perChannelStats_;
    AbandonCallback abandonCallback_;
};
