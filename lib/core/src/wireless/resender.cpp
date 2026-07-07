#include "wireless/resender.hpp"

#include "device/wireless-manager.hpp"
#include "device/drivers/logger.hpp"

namespace {
constexpr const char* RSND_TAG = "RSND";
}

void Resender::send(const uint8_t* target, PktType type, uint8_t seqId,
                    const uint8_t* payload, size_t len, SendMode mode) {
    if (target == nullptr) return;

    if (mode == SendMode::SUPERSEDE_PER_TARGET) {
        // State channel: a newer send obsoletes any prior unacked one to this
        // peer, so drop every prior entry regardless of seqId. Keeping a stale
        // one armed risks an old retransmit landing after the new state.
        eraseAllToTarget(type, target);
    } else {
        // Stream channel: distinct seqIds to the same peer coexist, so a batch
        // of reliable sends (e.g. one BRACKET_ENTRY per bracket slot) each keep
        // their own retry slot. Only a true resend of the same seqId replaces.
        std::vector<Pending>::iterator it = findPending(type, seqId, target);
        if (it != pending.end()) {
            pending.erase(it);
        }
    }

    Pending p;
    p.type = type;
    memcpy(p.target.data(), target, 6);
    p.seqId = seqId;
    p.payload.assign(payload, payload + len);
    p.retries = 0;
    p.timer.setTimer(backoffMs(0));
    pending.push_back(std::move(p));

    transmit(pending.back());
}

bool Resender::onAck(PktType type, uint8_t seqId, const uint8_t* fromMac) {
    if (fromMac == nullptr) return false;
    for (std::vector<Pending>::iterator it = pending.begin(); it != pending.end(); ++it) {
        if (it->type != type) continue;
        if (memcmp(it->target.data(), fromMac, 6) != 0) continue;
        if (it->seqId != seqId) continue;

        pending.erase(it);
        return true;
    }
    return false;
}

void Resender::cancel(PktType type, const uint8_t* target) {
    if (target == nullptr) return;
    eraseAllToTarget(type, target);
}

void Resender::eraseAllToTarget(PktType type, const uint8_t* target) {
    for (std::vector<Pending>::iterator it = pending.begin(); it != pending.end();) {
        if (it->type == type &&
            memcmp(it->target.data(), target, 6) == 0) {
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
}

void Resender::sync() {
    // Collect abandoned entries first so the abandon callback can safely
    // mutate pending (cancel, send) without invalidating this loop's
    // iteration. Retransmits stay inline because they don't structurally
    // change the vector.
    struct AbandonedEntry {
        PktType type;
        uint8_t seqId;
        std::array<uint8_t, 6> target;
    };
    std::vector<AbandonedEntry> abandoned;

    for (size_t i = 0; i < pending.size();) {
        Pending& p = pending[i];
        if (!p.timer.expired()) {
            ++i;
            continue;
        }

        if (p.retries >= MAX_RETRIES) {
            LOG_E(RSND_TAG, "abandon type=%u seq=%u to=%02X%02X",
                  (unsigned)p.type, p.seqId, p.target[4], p.target[5]);
            abandoned.push_back({p.type, p.seqId, p.target});
            pending.erase(pending.begin() + i);
            continue;
        }

        if (transmit(p)) {
            p.retries++;
            LOG_W(RSND_TAG, "retransmit type=%u seq=%u retry=%u to=%02X%02X",
                  (unsigned)p.type, p.seqId, p.retries, p.target[4], p.target[5]);
        }
        // Re-arm either way. A send that never reached the radio leaves retries
        // unchanged, so a packet that was not actually transmitted keeps being
        // retried rather than being abandoned against an exhausted budget.
        p.timer.setTimer(backoffMs(p.retries));
        ++i;
    }

    if (abandonCallback) {
        for (const AbandonedEntry& a : abandoned) {
            abandonCallback(a.type, a.seqId, a.target.data());
        }
    }
}

std::vector<Resender::Pending>::iterator Resender::findPending(
    PktType type, uint8_t seqId, const uint8_t* target) {
    for (std::vector<Pending>::iterator it = pending.begin(); it != pending.end(); ++it) {
        if (it->type != type) continue;
        if (it->seqId != seqId) continue;
        if (memcmp(it->target.data(), target, 6) != 0) continue;
        return it;
    }
    return pending.end();
}

bool Resender::transmit(const Pending& p) {
    // Null manager is the unit-test no-op path: nothing is sent, but nothing can
    // fail either, so report success and let retry bookkeeping run.
    if (wirelessManager == nullptr) return true;
    // A negative return means the frame never reached the radio (transient PSRAM
    // pressure, or a brief ESP-NOW-not-ready window during a WiFi mode switch).
    return wirelessManager->sendEspNowData(p.target.data(), p.type,
                                           p.payload.data(), p.payload.size()) >= 0;
}
