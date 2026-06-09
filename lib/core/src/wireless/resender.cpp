#include "wireless/resender.hpp"

#include "device/wireless-manager.hpp"

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

void Resender::send(const uint8_t* target, PktType type, uint8_t subType,
                    uint8_t seqId, const uint8_t* payload, size_t len,
                    SendMode mode) {
    if (target == nullptr) return;

    if (mode == SendMode::SupersedePerTarget) {
        // State channel: a newer send obsoletes any prior unacked one to this
        // peer, so drop every prior entry regardless of seqId. Keeping a stale
        // one armed risks an old retransmit landing after the new state.
        eraseAllToTarget(type, subType, target);
    } else {
        // Stream channel: distinct seqIds to the same peer coexist, so a batch
        // of reliable sends (e.g. one BRACKET_ENTRY per bracket slot) each keep
        // their own retry slot. Only a true resend of the same seqId replaces.
        auto it = findPending(type, subType, seqId, target);
        if (it != pending_.end()) {
            pending_.erase(it);
        }
    }

    Pending p;
    p.type = type;
    p.subType = subType;
    memcpy(p.target.data(), target, 6);
    p.seqId = seqId;
    p.payload.assign(payload, payload + len);
    p.retries = 0;
    p.timer.setTimer(kPolicy.backoffMs(0));
    pending_.push_back(std::move(p));

    statsFor(type, subType).sends++;

    transmit(pending_.back());
}

bool Resender::onAck(PktType type, uint8_t subType, uint8_t seqId,
                     const uint8_t* fromMac) {
    if (fromMac == nullptr) return false;
    // Each ReliableChannel allocates seqIds independently, so two channels
    // sharing a PktType (e.g. the seven ShootoutCmd subTypes) can have
    // identical (type, target, seqId) pending entries. Subtype must filter
    // or an ack routed to one channel will erase the other channel's pending
    // entry, abandoning it and triggering its abort path.
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->type != type) continue;
        if (it->subType != subType) continue;
        if (memcmp(it->target.data(), fromMac, 6) != 0) continue;
        if (it->seqId != seqId) continue;

        // Elapsed since the last (re)transmit, not since the first send: the
        // timer is rearmed on every retransmit in sync(). This is a resend-loop
        // health stat, not a true end-to-end round-trip.
        unsigned long latency = it->timer.getElapsedTime();
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
    eraseAllToTarget(type, subType, target);
}

void Resender::eraseAllToTarget(PktType type, uint8_t subType,
                                const uint8_t* target) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->type == type && it->subType == subType &&
            memcmp(it->target.data(), target, 6) == 0) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void Resender::sync() {
    // Collect abandoned entries first so the abandon callback can safely
    // mutate pending_ (cancel, send) without invalidating this loop's
    // iteration. Retransmits stay inline because they don't structurally
    // change the vector.
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

        if (p.retries >= kPolicy.maxRetries) {
            statsFor(p.type, p.subType).abandons++;
            abandoned.push_back({p.type, p.subType, p.seqId, p.target});
            pending_.erase(pending_.begin() + i);
            continue;
        }

        if (transmit(p)) {
            p.retries++;
            statsFor(p.type, p.subType).retries++;
        }
        // Re-arm either way. A send that never reached the radio leaves retries
        // unchanged, so a packet that was not actually transmitted keeps being
        // retried rather than being abandoned against an exhausted budget.
        p.timer.setTimer(kPolicy.backoffMs(p.retries));
        ++i;
    }

    if (abandonCallback_) {
        for (const auto& a : abandoned) {
            abandonCallback_(a.type, a.subType, a.seqId, a.target.data());
        }
    }
}

std::vector<Resender::Pending>::iterator Resender::findPending(
    PktType type, uint8_t subType, uint8_t seqId, const uint8_t* target) {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->type != type) continue;
        if (it->subType != subType) continue;
        if (it->seqId != seqId) continue;
        if (memcmp(it->target.data(), target, 6) != 0) continue;
        return it;
    }
    return pending_.end();
}

bool Resender::transmit(const Pending& p) {
    // Null manager is the unit-test no-op path: nothing is sent, but nothing can
    // fail either, so report success and let retry bookkeeping run.
    if (wm_ == nullptr) return true;
    // A negative return means the frame never reached the radio (transient PSRAM
    // pressure, or a brief ESP-NOW-not-ready window during a WiFi mode switch).
    return wm_->sendEspNowData(p.target.data(), p.type,
                               p.payload.data(), p.payload.size()) >= 0;
}
