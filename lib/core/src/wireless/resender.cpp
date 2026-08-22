#include "wireless/resender.hpp"

#include "device/wireless-manager.hpp"
#include "device/drivers/logger.hpp"

#include <algorithm>

namespace {
constexpr const char* RSND_TAG = "RSND";
}

void Resender::send(const uint8_t* target, PktType type, uint8_t seqId,
                    const uint8_t* payload, size_t len, SendMode mode) {
    if (target == nullptr) return;
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), target, 6);
    addGroup(type, seqId, mac, {mac}, payload, len, mode);
}

void Resender::sendBroadcast(const std::vector<std::array<uint8_t, 6>>& recipients,
                             PktType type, uint8_t seqId,
                             const uint8_t* payload, size_t len) {
    if (recipients.empty()) return;
    // The all-ones address is the ESP-NOW broadcast MAC. Held as a literal rather
    // than read from the radio so a fan-out is representable before the driver is
    // up, and so the unit-test path (null manager) takes the same route as
    // production instead of a second one.
    const std::array<uint8_t, 6> broadcast = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    addGroup(type, seqId, broadcast, recipients, payload, len, SendMode::KEEP_DISTINCT);
}

void Resender::addGroup(PktType type, uint8_t seqId,
                        const std::array<uint8_t, 6>& destination,
                        const std::vector<std::array<uint8_t, 6>>& recipients,
                        const uint8_t* payload, size_t len, SendMode mode) {
    if (mode == SendMode::SUPERSEDE_PER_TARGET) {
        supersedeRecipients(type, recipients);
    }
    // A re-send of the same frame — same seqId to the same address — replaces it
    // outright whatever the mode, since the recipients are whoever is named now
    // and a member that already acked must not be re-armed by it.
    //
    // The destination is part of that identity, not decoration. One channel can
    // address several peers out of a single seqId space, so the same seqId to a
    // different peer is a different frame; dropping it from the comparison would
    // erase a still-pending send to someone else, and that one would then never
    // retransmit and never abandon. An ack stays unambiguous across the two
    // because onAck matches the recipient's MAC as well as the seqId.
    for (std::vector<Group>::iterator it = groups.begin(); it != groups.end(); ++it) {
        if (it->type == type && it->seqId == seqId && it->destination == destination) {
            groups.erase(it);
            break;
        }
    }

    Group g;
    g.type = type;
    g.seqId = seqId;
    g.destination = destination;
    g.payload.assign(payload, payload + len);
    g.recipients.reserve(recipients.size());
    for (const std::array<uint8_t, 6>& mac : recipients) {
        Recipient r;
        r.target = mac;
        r.retries = 0;
        r.timer.setTimer(backoffMs(0));
        g.recipients.push_back(std::move(r));
    }
    groups.push_back(std::move(g));

    stats.sends++;
    transmit(groups.back());
}

void Resender::supersedeRecipients(PktType type,
                                   const std::vector<std::array<uint8_t, 6>>& recipients) {
    for (std::vector<Group>::iterator g = groups.begin(); g != groups.end();) {
        if (g->type != type) {
            ++g;
            continue;
        }
        for (std::vector<Recipient>::iterator r = g->recipients.begin();
             r != g->recipients.end();) {
            const bool superseded =
                std::find(recipients.begin(), recipients.end(), r->target) != recipients.end();
            r = superseded ? g->recipients.erase(r) : r + 1;
        }
        g = g->recipients.empty() ? groups.erase(g) : g + 1;
    }
}

bool Resender::onAck(PktType type, uint8_t seqId, const uint8_t* fromMac) {
    if (fromMac == nullptr) return false;
    for (std::vector<Group>::iterator g = groups.begin(); g != groups.end(); ++g) {
        if (g->type != type || g->seqId != seqId) continue;
        for (std::vector<Recipient>::iterator r = g->recipients.begin();
             r != g->recipients.end(); ++r) {
            if (memcmp(r->target.data(), fromMac, 6) != 0) continue;
            g->recipients.erase(r);
            // The frame is done once nobody is left owing an answer for it.
            if (g->recipients.empty()) groups.erase(g);
            return true;
        }
    }
    return false;
}

void Resender::cancel(PktType type, const uint8_t* target) {
    if (target == nullptr) return;
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), target, 6);
    supersedeRecipients(type, {mac});
}

void Resender::cancelAll(PktType type) {
    for (std::vector<Group>::iterator g = groups.begin(); g != groups.end();) {
        g = (g->type == type) ? groups.erase(g) : g + 1;
    }
}

void Resender::sync() {
    // Abandoned recipients are collected first so the callback can safely mutate
    // `groups` — send, cancel, cancelAll — without invalidating this iteration.
    // Retransmits stay inline because they do not structurally change it.
    std::vector<AbandonedEntry> abandoned;

    for (size_t gi = 0; gi < groups.size();) {
        Group& g = groups[gi];

        // One frame for the whole group, not one per recipient: that is the
        // point of a fan-out, and for a unicast the group holds exactly one.
        // Sent only if somebody is both due and still within budget, so a group
        // of nothing but exhausted recipients goes quiet.
        const bool anyDue = std::any_of(
            g.recipients.begin(), g.recipients.end(), [](Recipient& r) {
                return r.timer.expired() && r.retries < MAX_RETRIES;
            });
        const bool sent = anyDue ? transmit(g) : false;
        if (sent) {
            stats.retries++;
            // One line per round, not per recipient: a fan-out can carry dozens
            // and LOG_W is live in the release build.
            LOG_W(RSND_TAG, "retransmit type=%u seq=%u recipients=%u",
                  (unsigned)g.type, g.seqId, (unsigned)g.recipients.size());
        }
        // Whether a round the radio refused still costs a retry is the caller's
        // choice, not this loop's: see BudgetPolicy.
        const bool roundCounts = sent || budgetPolicy == BudgetPolicy::EVERY_ROUND;

        for (size_t ri = 0; ri < g.recipients.size();) {
            Recipient& r = g.recipients[ri];
            if (!r.timer.expired()) {
                ++ri;
                continue;
            }

            if (r.retries >= MAX_RETRIES) {
                LOG_E(RSND_TAG, "abandon type=%u seq=%u to=%02X%02X",
                      (unsigned)g.type, g.seqId, r.target[4], r.target[5]);
                abandoned.push_back({g.type, g.seqId, r.target, g.payload});
                stats.abandons++;
                g.recipients.erase(g.recipients.begin() + ri);
                continue;
            }
            if (roundCounts) r.retries++;
            r.timer.setTimer(backoffMs(r.retries));
            ++ri;
        }

        if (g.recipients.empty()) {
            groups.erase(groups.begin() + gi);
        } else {
            ++gi;
        }
    }

    if (abandonCallback) {
        for (const AbandonedEntry& a : abandoned) {
            abandonCallback(a.type, a.seqId, a.target.data(),
                            a.payload.data(), a.payload.size());
        }
    }
}

bool Resender::transmit(const Group& g) {
    // Null manager is the unit-test no-op path: nothing is sent, but nothing can
    // fail either, so report success and let retry bookkeeping run.
    if (wirelessManager == nullptr) return true;
    // A negative return means the frame never reached the radio (transient PSRAM
    // pressure, or a brief ESP-NOW-not-ready window during a WiFi mode switch).
    return wirelessManager->sendEspNowData(g.destination.data(), g.type,
                                           g.payload.data(), g.payload.size()) >= 0;
}
