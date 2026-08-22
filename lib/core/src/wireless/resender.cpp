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

    stats.sends++;
    transmit(pending.back());
}

void Resender::sendBroadcast(const std::vector<std::array<uint8_t, 6>>& members,
                             PktType type, uint8_t seqId,
                             const uint8_t* payload, size_t len) {
    if (members.empty()) return;

    // One group per (type, seqId): a re-send under the same seqId replaces the
    // previous one outright rather than leaving two claiming the same frame. The
    // recipients are whoever the caller names now, so a member that already acked
    // is re-armed if it is named again.
    for (std::vector<BroadcastGroup>::iterator it = broadcasts.begin();
         it != broadcasts.end(); ++it) {
        if (it->type == type && it->seqId == seqId) {
            broadcasts.erase(it);
            break;
        }
    }

    BroadcastGroup g;
    g.type = type;
    g.seqId = seqId;
    g.payload.assign(payload, payload + len);
    g.failedRounds = 0;
    g.members.reserve(members.size());
    for (const std::array<uint8_t, 6>& mac : members) {
        BroadcastMember m;
        m.target = mac;
        m.retries = 0;
        m.timer.setTimer(backoffMs(0));
        g.members.push_back(std::move(m));
    }
    broadcasts.push_back(std::move(g));

    stats.sends++;
    transmitBroadcast(broadcasts.back());
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
    // A fan-out member acking clears only its own slot; the rest of the group
    // stays armed, and the group goes when the last member is accounted for.
    for (std::vector<BroadcastGroup>::iterator g = broadcasts.begin();
         g != broadcasts.end(); ++g) {
        if (g->type != type || g->seqId != seqId) continue;
        for (std::vector<BroadcastMember>::iterator m = g->members.begin();
             m != g->members.end(); ++m) {
            if (memcmp(m->target.data(), fromMac, 6) != 0) continue;
            g->members.erase(m);
            if (g->members.empty()) broadcasts.erase(g);
            return true;
        }
    }
    return false;
}

void Resender::cancel(PktType type, const uint8_t* target) {
    if (target == nullptr) return;
    eraseAllToTarget(type, target);
    for (std::vector<BroadcastGroup>::iterator g = broadcasts.begin();
         g != broadcasts.end();) {
        if (g->type != type) {
            ++g;
            continue;
        }
        for (std::vector<BroadcastMember>::iterator m = g->members.begin();
             m != g->members.end();) {
            m = (memcmp(m->target.data(), target, 6) == 0) ? g->members.erase(m) : m + 1;
        }
        g = g->members.empty() ? broadcasts.erase(g) : g + 1;
    }
}

void Resender::cancelAll(PktType type) {
    for (std::vector<Pending>::iterator it = pending.begin(); it != pending.end();) {
        it = (it->type == type) ? pending.erase(it) : it + 1;
    }
    for (std::vector<BroadcastGroup>::iterator g = broadcasts.begin(); g != broadcasts.end();) {
        g = (g->type == type) ? broadcasts.erase(g) : g + 1;
    }
}

void Resender::syncBroadcasts(std::vector<AbandonedEntry>& abandoned) {
    for (size_t gi = 0; gi < broadcasts.size();) {
        BroadcastGroup& g = broadcasts[gi];

        // One frame for the whole group, not one per member: that is the entire
        // point of the fan-out. Sent only if somebody is both due and still
        // within budget, so a group of nothing but exhausted members goes quiet.
        bool anyDue = false;
        for (BroadcastMember& m : g.members) {
            if (m.timer.expired() && m.retries < MAX_RETRIES) {
                anyDue = true;
                break;
            }
        }
        const bool sent = anyDue ? transmitBroadcast(g) : false;
        if (anyDue) g.failedRounds = sent ? 0 : static_cast<uint8_t>(g.failedRounds + 1);
        // The radio is not coming back inside this frame's lifetime. Give the
        // whole group up so the caller hears about it rather than waiting on a
        // retry round that can never happen.
        const bool radioDown = g.failedRounds > MAX_RETRIES;
        if (anyDue && sent) {
            stats.retries++;
            // One line per round, not per recipient: a fan-out can carry dozens
            // and LOG_W is live in the release build.
            LOG_W(RSND_TAG, "retransmit type=%u seq=%u members=%u",
                  (unsigned)g.type, g.seqId, (unsigned)g.members.size());
        }

        for (size_t mi = 0; mi < g.members.size();) {
            BroadcastMember& m = g.members[mi];
            if (!m.timer.expired()) {
                ++mi;
                continue;
            }

            if (m.retries >= MAX_RETRIES || radioDown) {
                LOG_E(RSND_TAG, "abandon type=%u seq=%u to=%02X%02X",
                      (unsigned)g.type, g.seqId, m.target[4], m.target[5]);
                abandoned.push_back({g.type, g.seqId, m.target, g.payload});
                stats.abandons++;
                g.members.erase(g.members.begin() + mi);
                continue;
            }
            // Only a frame that actually reached the radio costs a retry, and
            // the whole group shares that one outcome.
            if (sent) m.retries++;
            m.timer.setTimer(backoffMs(m.retries));
            ++mi;
        }

        if (g.members.empty()) {
            broadcasts.erase(broadcasts.begin() + gi);
        } else {
            ++gi;
        }
    }
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
            abandoned.push_back({p.type, p.seqId, p.target, p.payload});
            stats.abandons++;
            pending.erase(pending.begin() + i);
            continue;
        }

        if (transmit(p)) {
            p.retries++;
            stats.retries++;
            LOG_W(RSND_TAG, "retransmit type=%u seq=%u retry=%u to=%02X%02X",
                  (unsigned)p.type, p.seqId, p.retries, p.target[4], p.target[5]);
        }
        // Re-arm either way. A send that never reached the radio leaves retries
        // unchanged, so a packet that was not actually transmitted keeps being
        // retried rather than being abandoned against an exhausted budget.
        p.timer.setTimer(backoffMs(p.retries));
        ++i;
    }

    syncBroadcasts(abandoned);

    if (abandonCallback) {
        for (const AbandonedEntry& a : abandoned) {
            abandonCallback(a.type, a.seqId, a.target.data(),
                            a.payload.data(), a.payload.size());
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

bool Resender::transmitBroadcast(const BroadcastGroup& g) {
    // Null manager is the unit-test no-op path; see transmit().
    if (wirelessManager == nullptr) return true;
    return wirelessManager->sendEspNowData(wirelessManager->getBroadcastAddress(), g.type,
                                           g.payload.data(), g.payload.size()) >= 0;
}
