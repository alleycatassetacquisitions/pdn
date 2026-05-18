#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <type_traits>
#include <vector>

#include "utils/simple-timer.hpp"
#include "wireless/peer-comms-types.hpp"

class WirelessManager;

// Reliable unicast: send a packet to a peer, retransmit on timeout, abandon
// after maxRetries. One pending entry per (PktType, subType, target). sync()
// must be called every loop tick to drive retransmits.
class Resender {
public:
    struct RetryPolicy {
        unsigned long initialTimeoutMs = 100;
        uint8_t maxRetries = 3;
        // exponential: 100, 200, 400 ... ; non-exponential: constant initialTimeoutMs.
        bool exponentialBackoff = true;
    };

    struct Stats {
        uint32_t sends = 0;
        uint32_t retries = 0;
        uint32_t abandons = 0;
        uint32_t ackLatencyMsSum = 0;
        uint32_t ackCount = 0;
    };

    // Fires once per pending entry that exhausts its retry budget. Invoked
    // from sync() AFTER all retransmits have been processed and the
    // abandoned entries removed, so callbacks may freely call send(),
    // cancel(), or cancelChannel() on this Resender without invalidating
    // the iteration.
    using AbandonCallback = std::function<void(PktType type, uint8_t subType,
                                               uint8_t seqId,
                                               const uint8_t* targetMac)>;

    explicit Resender(WirelessManager* wirelessManager)
        : wm_(wirelessManager) {
        registerInstance(this);
    }
    ~Resender() { unregisterInstance(this); }

    // Send a kAck for a reliably-received packet. All subsystems share
    // this one ack wire format.
    static void sendAck(WirelessManager* wm, const uint8_t* toMac,
                        PktType originalType, uint8_t subType, uint8_t seqId);
    template <class C, class = std::enable_if_t<std::is_enum_v<C>>>
    static void sendAck(WirelessManager* wm, const uint8_t* toMac,
                        PktType originalType, C channel, uint8_t seqId) {
        sendAck(wm, toMac, originalType, toSubType(channel), seqId);
    }

    // Routes incoming kAck to ownerOf(type), or walks every instance as
    // fallback. Matches pending by (type, target, seqId); subType ignored.
    //
    // Disjointness invariant: distinct Resender instances must own disjoint
    // PktType sets, else ack routing is ambiguous. Violations counted via
    // getOwnershipViolationCount() without aborting.
    static void processIncomingAck(const uint8_t* fromMac,
                                   const uint8_t* data, size_t len);

    // First send() under a PktType claims ownership; destruction releases it.
    static Resender* ownerOf(PktType type);

    static uint32_t getOwnershipViolationCount();

    void setPolicy(PktType type, uint8_t subType, RetryPolicy policy) {
        policies_[makeKey(type, subType)] = policy;
    }
    void setPolicy(PktType type, RetryPolicy policy) {
        setPolicy(type, 0, policy);
    }

    void setAbandonCallback(AbandonCallback cb) {
        abandonCallback_ = std::move(cb);
    }

    template <class C,
              class = std::enable_if_t<std::is_enum_v<C> || std::is_integral_v<C>>>
    static constexpr uint8_t toSubType(C c) {
        return static_cast<uint8_t>(c);
    }

    // send() with the same (type, subType, target) replaces any prior pending.
    // payload bytes are copied.
    void send(const uint8_t* target, PktType type, uint8_t subType,
              uint8_t seqId, const uint8_t* payload, size_t len);
    template <class C, class = std::enable_if_t<std::is_enum_v<C>>>
    void send(const uint8_t* target, PktType type, C channel,
              uint8_t seqId, const uint8_t* payload, size_t len) {
        send(target, type, toSubType(channel), seqId, payload, len);
    }
    void send(const uint8_t* target, PktType type, uint8_t seqId,
              const uint8_t* payload, size_t len) {
        send(target, type, uint8_t{0}, seqId, payload, len);
    }

    bool onAck(PktType type, uint8_t subType, uint8_t seqId, const uint8_t* fromMac);
    template <class C, class = std::enable_if_t<std::is_enum_v<C>>>
    bool onAck(PktType type, C channel, uint8_t seqId, const uint8_t* fromMac) {
        return onAck(type, toSubType(channel), seqId, fromMac);
    }
    bool onAck(PktType type, uint8_t seqId, const uint8_t* fromMac) {
        return onAck(type, uint8_t{0}, seqId, fromMac);
    }

    // Silent drop; use when the target is known unreachable. No abandon callback.
    void cancel(PktType type, uint8_t subType, const uint8_t* target);
    template <class C, class = std::enable_if_t<std::is_enum_v<C>>>
    void cancel(PktType type, C channel, const uint8_t* target) {
        cancel(type, toSubType(channel), target);
    }
    void cancel(PktType type, const uint8_t* target) {
        cancel(type, uint8_t{0}, target);
    }

    void cancelChannel(PktType type, uint8_t subType = 0);
    template <class C, class = std::enable_if_t<std::is_enum_v<C>>>
    void cancelChannel(PktType type, C channel) {
        cancelChannel(type, toSubType(channel));
    }

    void cancelAll() { pending_.clear(); }

    // Must be called every loop tick.
    void sync();

    Stats getStats() const { return stats_; }
    Stats getStats(PktType type, uint8_t subType = 0) const {
        auto it = perChannelStats_.find(makeKey(type, subType));
        if (it == perChannelStats_.end()) return Stats{};
        return it->second;
    }

    size_t pendingCount() const { return pending_.size(); }
    size_t pendingCount(PktType type, uint8_t subType = 0) const {
        size_t count = 0;
        for (const auto& p : pending_) {
            if (p.type == type && p.subType == subType) ++count;
        }
        return count;
    }
    template <class C, class = std::enable_if_t<std::is_enum_v<C>>>
    size_t pendingCount(PktType type, C channel) const {
        return pendingCount(type, toSubType(channel));
    }

    bool isPending(PktType type, uint8_t subType, const uint8_t* target) const {
        if (target == nullptr) return false;
        for (const auto& p : pending_) {
            if (p.type != type || p.subType != subType) continue;
            if (memcmp(p.target.data(), target, 6) == 0) return true;
        }
        return false;
    }
    template <class C, class = std::enable_if_t<std::is_enum_v<C>>>
    bool isPending(PktType type, C channel, const uint8_t* target) const {
        return isPending(type, toSubType(channel), target);
    }

private:
    using Key = uint32_t;  // (subType << 16) | static_cast<uint8_t>(type)

    static Key makeKey(PktType type, uint8_t subType) {
        return (static_cast<Key>(subType) << 16) |
               static_cast<Key>(static_cast<uint8_t>(type));
    }

    struct Pending {
        PktType type;
        uint8_t subType;
        std::array<uint8_t, 6> target;
        uint8_t seqId;
        std::vector<uint8_t> payload;
        uint8_t retries;
        RetryPolicy policy;
        SimpleTimer timer;
    };

    RetryPolicy policyFor(PktType type, uint8_t subType) const {
        auto it = policies_.find(makeKey(type, subType));
        if (it == policies_.end()) return RetryPolicy{};
        return it->second;
    }

    static unsigned long backoffMs(const RetryPolicy& p, uint8_t retryNum) {
        if (!p.exponentialBackoff) return p.initialTimeoutMs;
        return p.initialTimeoutMs << retryNum;
    }

    Stats& statsFor(PktType type, uint8_t subType) {
        return perChannelStats_[makeKey(type, subType)];
    }

    std::vector<Pending>::iterator findPending(
        PktType type, uint8_t subType, const uint8_t* target);

    void transmit(const Pending& p);

    static void registerInstance(Resender* r);
    static void unregisterInstance(Resender* r);

    void claimType(PktType type);

    WirelessManager* wm_;
    std::vector<Pending> pending_;
    std::map<Key, RetryPolicy> policies_;
    std::map<Key, Stats> perChannelStats_;
    Stats stats_;
    AbandonCallback abandonCallback_;
    std::vector<uint8_t> claimedTypes_;
};
