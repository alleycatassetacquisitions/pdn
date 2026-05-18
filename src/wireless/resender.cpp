#include "wireless/resender.hpp"

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"

namespace {
// All live Resender instances; used by ownerOf() for ack routing.
std::vector<Resender*>& instances() {
    static std::vector<Resender*> v;
    return v;
}

uint32_t& ownershipViolations() {
    static uint32_t n = 0;
    return n;
}
}

void Resender::registerInstance(Resender* r) {
    instances().push_back(r);
}

void Resender::unregisterInstance(Resender* r) {
    auto& v = instances();
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (*it == r) { v.erase(it); return; }
    }
}

void Resender::sendAck(WirelessManager* wm, const uint8_t* toMac,
                      PktType originalType, uint8_t subType, uint8_t seqId) {
    if (wm == nullptr || toMac == nullptr) return;
    // seqId=0 is the fire-and-forget sentinel; no sender-side pending entry
    // exists for the ack to match, so emitting one wastes airtime.
    if (seqId == 0) return;
    AckPayload payload{static_cast<uint8_t>(originalType), subType, seqId};
    wm->sendEspNowData(toMac, PktType::kAck,
                       reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
}

void Resender::processIncomingAck(const uint8_t* fromMac,
                                  const uint8_t* data, size_t len) {
    if (len != sizeof(AckPayload)) {
        LOG_E("Resender", "AckPayload size mismatch: got %u, expected %u (possible firmware mismatch)",
              (unsigned)len, (unsigned)sizeof(AckPayload));
        return;
    }
    const AckPayload* p = reinterpret_cast<const AckPayload*>(data);
    if (p->originalType >= static_cast<uint8_t>(PktType::kNumPacketTypes)) return;
    PktType originalType = static_cast<PktType>(p->originalType);

    Resender* owner = ownerOf(originalType);
    if (owner != nullptr) {
        owner->onAck(originalType, p->subType, p->seqId, fromMac);
        return;
    }
    // Fallback when no instance has claimed the type yet: walk all
    // instances so a future claimant still receives the ack.
    for (Resender* r : instances()) {
        r->onAck(originalType, p->subType, p->seqId, fromMac);
    }
}

void Resender::send(const uint8_t* target, PktType type, uint8_t subType,
                    uint8_t seqId, const uint8_t* payload, size_t len) {
    if (target == nullptr) return;

    claimType(type);

    auto it = findPending(type, subType, target);
    if (it != pending_.end()) {
        pending_.erase(it);
    }

    Pending p;
    p.type = type;
    p.subType = subType;
    memcpy(p.target.data(), target, 6);
    p.seqId = seqId;
    p.payload.assign(payload, payload + len);
    p.retries = 0;
    p.policy = policyFor(type, subType);
    p.timer.setTimer(backoffMs(p.policy, 0));
    pending_.push_back(std::move(p));

    stats_.sends++;
    statsFor(type, subType).sends++;

    transmit(pending_.back());
}

bool Resender::onAck(PktType type, uint8_t /*subType*/, uint8_t seqId,
                     const uint8_t* fromMac) {
    if (fromMac == nullptr) return false;
    // Match by (type, target, seqId) only; subType is sender-only state
    // the receiver cannot reconstruct (RDC uses its port index there).
    // seqId is unique per sender's (type), so the triple is sufficient.
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->type != type) continue;
        if (memcmp(it->target.data(), fromMac, 6) != 0) continue;
        if (it->seqId != seqId) continue;

        unsigned long latency = it->timer.getElapsedTime();
        stats_.ackLatencyMsSum += latency;
        stats_.ackCount++;
        Stats& s = statsFor(it->type, it->subType);
        s.ackLatencyMsSum += latency;
        s.ackCount++;

        pending_.erase(it);
        return true;
    }
    return false;
}

void Resender::cancel(PktType type, uint8_t subType, const uint8_t* target) {
    if (target == nullptr) return;
    auto it = findPending(type, subType, target);
    if (it != pending_.end()) pending_.erase(it);
}

void Resender::cancelChannel(PktType type, uint8_t subType) {
    for (auto it = pending_.begin(); it != pending_.end(); ) {
        if (it->type == type && it->subType == subType) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void Resender::sync() {
    // Collect abandoned entries first so the abandon callback can safely
    // mutate pending_ (cancel, send, cancelChannel) without invalidating
    // this loop's iteration. Retransmits stay inline because they don't
    // structurally change the vector.
    struct AbandonedEntry {
        PktType type;
        uint8_t subType;
        uint8_t seqId;
        std::array<uint8_t, 6> target;
    };
    std::vector<AbandonedEntry> abandoned;

    for (size_t i = 0; i < pending_.size(); ) {
        Pending& p = pending_[i];
        if (!p.timer.expired()) { ++i; continue; }

        if (p.retries >= p.policy.maxRetries) {
            stats_.abandons++;
            statsFor(p.type, p.subType).abandons++;
            abandoned.push_back({p.type, p.subType, p.seqId, p.target});
            pending_.erase(pending_.begin() + i);
            continue;
        }

        p.retries++;
        stats_.retries++;
        statsFor(p.type, p.subType).retries++;
        transmit(p);
        p.timer.setTimer(backoffMs(p.policy, p.retries));
        ++i;
    }

    if (abandonCallback_) {
        for (const auto& a : abandoned) {
            abandonCallback_(a.type, a.subType, a.seqId, a.target.data());
        }
    }
}

std::vector<Resender::Pending>::iterator Resender::findPending(
    PktType type, uint8_t subType, const uint8_t* target) {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->type != type) continue;
        if (it->subType != subType) continue;
        if (memcmp(it->target.data(), target, 6) != 0) continue;
        return it;
    }
    return pending_.end();
}

void Resender::transmit(const Pending& p) {
    if (wm_ == nullptr) return;
    wm_->sendEspNowData(p.target.data(), p.type, p.payload.data(), p.payload.size());
}

void Resender::claimType(PktType type) {
    uint8_t key = static_cast<uint8_t>(type);
    for (uint8_t t : claimedTypes_) {
        if (t == key) return;  // already claimed by self
    }
    for (Resender* r : instances()) {
        if (r == this) continue;
        for (uint8_t t : r->claimedTypes_) {
            if (t == key) {
                ownershipViolations()++;
                LOG_E("Resender",
                      "PktType %u co-owned by multiple Resender instances "
                      "(this=%p other=%p); ack routing will be ambiguous",
                      static_cast<unsigned>(key),
                      static_cast<const void*>(this),
                      static_cast<const void*>(r));
                claimedTypes_.push_back(key);
                return;
            }
        }
    }
    claimedTypes_.push_back(key);
}

Resender* Resender::ownerOf(PktType type) {
    uint8_t key = static_cast<uint8_t>(type);
    for (Resender* r : instances()) {
        for (uint8_t t : r->claimedTypes_) {
            if (t == key) return r;
        }
    }
    return nullptr;
}

uint32_t Resender::getOwnershipViolationCount() {
    return ownershipViolations();
}
