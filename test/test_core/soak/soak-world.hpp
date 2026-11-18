#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "id-generator.hpp"

#include "sim-devices.hpp"
#include "soak-actions.hpp"
#include "soak-hooks.hpp"

namespace soak {

inline constexpr uint64_t TICK_MS = 5;
// The drain has no cables and no presses, only radio, flash and HTTP waiting out the
// uploader's 300 s interval: a HELLO-aligned 20 ms tick is well inside every
// retry window (100 ms resend, 500 ms context) and keeps the CI smoke runs short.
inline constexpr uint64_t DRAIN_TICK_MS = 20;
inline constexpr uint64_t HELLO_CADENCE_MS = 20;
// The event's wall clock at virtual time zero (2026-11-18).
inline constexpr int64_t EVENT_EPOCH_MS = 1795000000000LL;
// A synced PDN stamps occurred-at from a clock set by an FDN context that crossed
// the medium: off by at most that frame's delay plus the context resend span, then
// by its drift since. A cabled PDN sees its context within the 500 ms exchange
// timeout, retried once, so 1500 ms plus 100 ppm of the time since sync.
inline constexpr int64_t OCCURRED_AT_BOUND_MS = 1500;
inline constexpr int32_t MAX_DRIFT_PPM = 100;
inline constexpr uint32_t PITY_FLOOR_MS = 750;

struct SoakConfig {
    ScheduleConfig schedule;
    MediumConfig medium;
    HttpFaults http;
    uint64_t drainCapMs = 40 * 60000;
    // Empty: the in-process fake server. Else a loopback alleycat-rs.
    std::string serverUrl;
};

struct RunResult {
    bool passed = false;
    std::string summary;
    std::vector<std::string> failures;
    std::string log;
    uint64_t logHash = 0;
    std::map<std::string, uint64_t> counts;
};

/** FNV-1a over @p text. */
inline uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 1469598103934665603ULL;
    for (char c : text)
        hash = (hash ^ static_cast<uint8_t>(c)) * 1099511628211ULL;
    return hash;
}

/** Lowercase canonical text for a UUID-shaped id; other ids unchanged. */
inline std::string canonicalId(const char* id) {
    UUIDBytes parsed;
    return UUIDBytes::parse(id, parsed) ? formatUuid(parsed) : std::string(id);
}

// One simulated event: PDNs, FDNs, medium, cables, a server, and the ground truth
// of every duel, checked end to end after a drain.
class World {
public:
    /** Builds the event for @p config and will play @p actions. */
    World(const SoakConfig& config, const std::vector<Action>& actions)
        : config(config)
        , actions(actions)
        , medium(config.schedule.seed ^ 0x11, now)
        , cables([this](const JackRef& jack) { return resolveJack(jack); }) {
        medium.config = config.medium;
    }

    /** Not copyable: devices hold references into the world. */
    World(const World&) = delete;
    /** Not assignable. */
    World& operator=(const World&) = delete;

    /** Frees every device. */
    ~World() {
        for (PdnSlot& slot : pdns) {
            enter(slot.clock, &slot.logger);
            delete slot.device;
            delete slot.clock;
        }
        for (FdnSlot& slot : fdns) {
            enter(slot.clock, &slot.logger);
            delete slot.device;
            delete slot.clock;
        }
        delete server;
        SimpleTimer::setPlatformClock(savedClock);
        g_logger = savedLogger;
        releaseEntropy();
    }

    /** Plays the actions, drains, checks, and reports. */
    RunResult run() {
        savedClock = SimpleTimer::getPlatformClock();
        savedLogger = g_logger;
        if (!setUp()) return finish(false);
        const uint64_t horizon = config.schedule.horizonMs;
        while (now < horizon || nextAction < actions.size() || !deferred.empty())
            tick();
        while (!activities.empty() && now < horizon + 300000)
            tick();
        startDrain();
        const uint64_t cap = now + config.drainCapMs;
        while (now < cap && !(now % 1000 < DRAIN_TICK_MS && isQuiescent()))
            tick();
        return finish(isQuiescent());
    }

private:
    struct PdnSlot {
        SimPdn* device = nullptr;
        SimNvs nvs;
        VirtualClock* clock = nullptr;
        SoakLogger logger;
        uint8_t mac[6] = {};
        PdnIdentity identity;
        bool down = false;
        bool powerLost = false;
        bool destroyPending = false;
        uint64_t upAt = 0;
        int activity = -1;
        int state = -1;
        uint64_t stateSince = 0;
        bool sawCountdown = false;
        std::string matchId;
        unsigned long matchStart = 0;
        bool planned = false;
        int64_t reaction = -1;
        int64_t mash = 0;
        int64_t mashDone = 0;
        bool confirm = false;
        bool clicked = false;
        int64_t rebootDelay = -1;
        uint64_t rebootAt = 0;
        uint32_t matchOrdinal = 0;
        std::string committedThisTick;
        std::map<std::string, std::string> legacy;
    };
    struct FdnSlot {
        SimFdn* device = nullptr;
        SimNvs nvs;
        VirtualClock* clock = nullptr;
        SoakLogger logger;
        uint8_t mac[6] = {};
        HttpFaults faults;
        std::mt19937_64 rng;
        bool down = false;
        bool destroyPending = false;
        uint64_t upAt = 0;
        uint64_t mutedUntil = 0;
        uint64_t offlineUntil = 0;
    };
    struct CableOp {
        uint64_t at;
        bool plug;
        JackRef a;
        JackRef b;
    };
    struct Activity {
        size_t action;
        std::vector<int> pdns;
        std::vector<CableOp> ops;
        std::vector<std::pair<JackRef, JackRef>> plugged;
        uint64_t deadline = 0;
        int cutSlot = -1;
    };
    struct Side {
        int slot = -1;
        bool pressed = false;
        bool finalized = false;
        uint32_t raw = 0;
        uint32_t boost = 0;
    };
    struct TruthMatch {
        bool shootout = false;
        Side hunter;
        Side bounty;
        std::string disturbed;
    };
    struct Commit {
        int slot;
        MatchRecord record;
        std::string id;
        bool synced;
        int64_t trueUnixMs;
        uint64_t sinceSyncMs;
    };

    // ---- set-up -----------------------------------------------------------

    void enter(PlatformClock* clock, LoggerInterface* logger) {
        SimpleTimer::setPlatformClock(clock);
        g_logger = logger;
    }

    void note(const std::string& line) { log += "t=" + std::to_string(now) + " " + line + "\n"; }

    void fail(const std::string& message) { failures.push_back(message); }

    bool setUp() {
        const uint64_t seed = config.schedule.seed;
        uint64_t nonce = 0;
        if (config.serverUrl.empty()) {
            fake = new FakeServer(seed ^ 0x22);
            server = fake;
        } else {
            RealServer* real = new RealServer(config.serverUrl);
            server = real;
            if (!real->isUsable()) {
                fail("SOAK_SERVER_URL must be http://127.0.0.1:<port> or http://localhost:<port>");
                return false;
            }
            // Match ids are unique per event on a real server; mix the event into the entropy.
            nonce = static_cast<uint64_t>(time(nullptr));
        }
        seedEntropy(seed ^ nonce);
        IdGenerator::initialize(static_cast<unsigned long>(seed ^ nonce));
        srand(static_cast<unsigned>(seed));
        std::mt19937_64 rng(seed ^ 0x33);
        Team teams[2];
        strncpy(teams[0].name, "Hunter", sizeof(teams[0].name) - 1);
        strncpy(teams[1].name, "Bounty", sizeof(teams[1].name) - 1);
        if (!server->openEvent("soak-" + std::to_string(seed) + "-" + std::to_string(nonce)) ||
            !server->addTeam("Hunter", teams[0].id) || !server->addTeam("Bounty", teams[1].id)) {
            fail("server set-up failed (is the local server up?)");
            return false;
        }
        pdns.resize(static_cast<size_t>(config.schedule.pdns));
        for (size_t i = 0; i < pdns.size(); i++) {
            PdnSlot& slot = pdns[i];
            const uint8_t mac[6] = {
            0x02, 0x50, 0, 0, static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
            memcpy(slot.mac, mac, 6);
            slot.identity.hunter = i % 2 == 0;
            slot.identity.team = teams[i % 2];
            if (!server->addPlayer("pdn-" + std::to_string(i), slot.identity.team.id,
                                   slot.identity.playerId)) {
                fail("server player registration failed");
                return false;
            }
            const int32_t drift =
            static_cast<int32_t>(rng() % (2 * MAX_DRIFT_PPM + 1)) - MAX_DRIFT_PPM;
            slot.clock = new VirtualClock(now, drift);
            const int index = static_cast<int>(i);
            slot.nvs.onWrite = [this, index](const std::string& ns, const std::string& key) {
                onPdnWrite(index, ns, key);
            };
            slot.nvs.onCut = [this, index]() { medium.setMuted(pdns[index].mac, true); };
            if (rng() % 3 == 0) seedLegacyMatches(slot, rng);
            bootPdn(index);
        }
        fdns.resize(static_cast<size_t>(config.schedule.fdns));
        for (size_t i = 0; i < fdns.size(); i++) {
            FdnSlot& slot = fdns[i];
            const uint8_t mac[6] = {0x02, 0xFD, 0, 0, 0, static_cast<uint8_t>(i)};
            memcpy(slot.mac, mac, 6);
            slot.rng.seed(seed ^ (0x44 + i));
            slot.faults = config.http;
            slot.clock = new VirtualClock(now, 0);
            slot.nvs.onCut = [this, i]() { medium.setMuted(fdns[i].mac, true); };
            bootFdn(static_cast<int>(i));
        }
        medium.onAir = [this](const uint8_t* src, PktType type, const std::vector<uint8_t>& data) {
            (void)src;
            if (type != PktType::kMatchRecord || data.size() != sizeof(MatchRecordFramePacket))
                return;
            MatchRecord record;
            memcpy(&record, data.data() + 6, sizeof(record));
            airIds.insert(formatUuid(record.matchUuid));
        };
        for (const Action& action : actions)
            note("plan " + formatAction(action));
        ready = true;
        return true;
    }

    // Older firmware's JSON records (match-manager-tests.hpp:999), under ids no
    // current match can have; they must never move.
    void seedLegacyMatches(PdnSlot& slot, std::mt19937_64& rng) {
        const uint8_t count = static_cast<uint8_t>(1 + rng() % 3);
        slot.nvs.put(LEGACY_MATCHES_NAMESPACE_NAME, "count", 'c',
                     std::string(1, static_cast<char>(count)));
        for (uint8_t i = 0; i < count; i++) {
            UUIDBytes id;
            for (uint8_t& byte : id.bytes)
                byte = static_cast<uint8_t>(rng());
            legacyIds.insert(formatUuid(id));
            slot.nvs.put(LEGACY_MATCHES_NAMESPACE_NAME, "match_" + std::to_string(i), 's',
                         "{\"match_id\":\"" + formatUuid(id) +
                         "\",\"hunter\":\"1111\",\"bounty\":\"2222\",\"winner_is_hunter\":true}");
        }
        slot.legacy = slot.nvs.snapshot(LEGACY_MATCHES_NAMESPACE_NAME);
    }

    void bootPdn(int index) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        slot.clock->boot(slot.powerLost);
        slot.nvs.thaw();
        medium.setMuted(slot.mac, false);
        enter(slot.clock, &slot.logger);
        slot.device =
        new SimPdn("_" + std::to_string(index), medium, slot.mac, slot.nvs, slot.identity);
        slot.down = slot.powerLost = false;
        slot.state = -1;
        slot.matchId.clear();
        note("up pdn=" + std::to_string(index));
    }

    void bootFdn(int index) {
        FdnSlot& slot = fdns[static_cast<size_t>(index)];
        slot.clock->boot(true);
        // The FDN's wall clock is taken as right: it reads the server's Date header.
        slot.clock->setUnixMilliseconds(EVENT_EPOCH_MS + static_cast<int64_t>(now));
        slot.nvs.thaw();
        medium.setMuted(slot.mac, now < slot.mutedUntil);
        enter(slot.clock, &slot.logger);
        slot.device = new SimFdn("_f" + std::to_string(index), medium, slot.mac, slot.nvs,
                                 server->deviceBackend(), slot.faults, slot.rng);
        slot.down = false;
        note("up fdn=" + std::to_string(index));
    }

    NativeSerialDriver* resolveJack(const JackRef& jack) {
        if (jack.device >= 0) {
            PdnSlot& slot = pdns[static_cast<size_t>(jack.device)];
            return slot.device != nullptr ? slot.device->jack(jack.jack) : nullptr;
        }
        FdnSlot& slot = fdns[static_cast<size_t>(-jack.device - 1)];
        return slot.device != nullptr ? slot.device->jack(jack.jack) : nullptr;
    }

    // ---- the tick ---------------------------------------------------------

    void tick() {
        now += draining ? DRAIN_TICK_MS : TICK_MS;
        medium.deliverDue();
        if (noEventUntil != 0 && now >= noEventUntil) {
            noEventUntil = 0;
            fake->eventActive = true;
        }
        cables.transfer(allJacks());
        applyActions();
        for (size_t i = 0; i < activities.size();) {
            if (updateActivity(activities[i])) {
                activities.erase(activities.begin() + static_cast<long>(i));
            } else {
                i++;
            }
        }
        const bool hello = now % HELLO_CADENCE_MS == 0;
        for (size_t i = 0; i < pdns.size(); i++) {
            PdnSlot& slot = pdns[i];
            if (slot.down) {
                if (now >= slot.upAt) bootPdn(static_cast<int>(i));
                continue;
            }
            enter(slot.clock, &slot.logger);
            slot.committedThisTick.clear();
            if (hello) slot.device->pdn->getRemoteDeviceCoordinator()->emitHello();
            slot.device->pdn->loop();
            autopilot(static_cast<int>(i));
            if (slot.nvs.isFrozen() || (slot.rebootAt != 0 && now >= slot.rebootAt)) {
                takeDown(static_cast<int>(i), slot.nvs.isFrozen(), 0);
            }
        }
        for (size_t i = 0; i < fdns.size(); i++) {
            FdnSlot& slot = fdns[i];
            if (slot.down) {
                if (now >= slot.upAt) bootFdn(static_cast<int>(i));
                continue;
            }
            if (slot.mutedUntil != 0 && now >= slot.mutedUntil) {
                slot.mutedUntil = 0;
                medium.setMuted(slot.mac, false);
            }
            if (slot.offlineUntil != 0 && now >= slot.offlineUntil) {
                slot.offlineUntil = 0;
                slot.faults.offline = config.http.offline;
            }
            enter(slot.clock, &slot.logger);
            if (hello) slot.device->fdn->getRemoteDeviceCoordinator()->emitHello();
            slot.device->fdn->loop();
            if (slot.nvs.isFrozen()) takeDownFdn(static_cast<int>(i), 1500);
        }
    }

    std::vector<JackRef> allJacks() const {
        std::vector<JackRef> out;
        for (size_t i = 0; i < pdns.size(); i++) {
            out.push_back({static_cast<int>(i), SerialIdentifier::OUTPUT_JACK});
            out.push_back({static_cast<int>(i), SerialIdentifier::INPUT_JACK});
        }
        for (size_t i = 0; i < fdns.size(); i++) {
            out.push_back({-static_cast<int>(i) - 1, SerialIdentifier::INPUT_JACK});
            out.push_back({-static_cast<int>(i) - 1, SerialIdentifier::INPUT_JACK_SECONDARY});
        }
        return out;
    }

    // A reboot (soft) or a power cut: the device is gone for its boot time.
    void takeDown(int index, bool powerLost, uint64_t downMs) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        if (slot.down) return;
        enter(slot.clock, &slot.logger);
        delete slot.device;
        slot.device = nullptr;
        slot.down = true;
        slot.powerLost = powerLost;
        slot.rebootAt = 0;
        slot.rebootDelay = -1;
        slot.upAt = now + (downMs != 0 ? downMs : 1500);
        medium.setMuted(slot.mac, true);
        for (std::pair<const std::string, TruthMatch>& entry : truth) {
            TruthMatch& match = entry.second;
            const bool open = (match.hunter.slot == index && !match.hunter.finalized) ||
                              (match.bounty.slot == index && !match.bounty.finalized);
            if (open && match.disturbed.empty()) match.disturbed = "reboot";
        }
        note(std::string(powerLost ? "cut" : "reboot") + " pdn=" + std::to_string(index));
    }

    void takeDownFdn(int index, uint64_t downMs) {
        FdnSlot& slot = fdns[static_cast<size_t>(index)];
        if (slot.down) return;
        enter(slot.clock, &slot.logger);
        delete slot.device;
        slot.device = nullptr;
        slot.down = true;
        slot.upAt = now + downMs;
        medium.setMuted(slot.mac, true);
        note("down fdn=" + std::to_string(index));
    }

    // ---- actions and activities --------------------------------------------

    static SerialIdentifier opponentJack(bool hunter) {
        return hunter ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
    }
    static SerialIdentifier supporterJack(bool hunter) {
        return hunter ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
    }

    bool isReady(int pdn) const {
        const PdnSlot& slot = pdns[static_cast<size_t>(pdn)];
        return !slot.down && slot.activity < 0 && slot.state == IDLE &&
               !cables.isPlugged({pdn, SerialIdentifier::OUTPUT_JACK}) &&
               !cables.isPlugged({pdn, SerialIdentifier::INPUT_JACK});
    }

    void applyActions() {
        while (nextAction < actions.size() && actions[nextAction].at <= now) {
            deferred.push_back(nextAction++);
        }
        for (size_t i = 0; i < deferred.size();) {
            const Action& action = actions[deferred[i]];
            const int result = tryApply(deferred[i]);
            if (result == 0 && now < action.at + 30000) {
                i++;
                continue;
            }
            note(std::string(result != 0 ? "apply " : "skip ") + std::to_string(deferred[i]));
            counts[result != 0 ? "actionsApplied" : "actionsSkipped"]++;
            deferred.erase(deferred.begin() + static_cast<long>(i));
        }
    }

    // 1 applied, 0 not yet possible.
    int tryApply(size_t index) {
        const Action& action = actions[index];
        switch (action.type) {
            case ActionType::DUEL: return startDuel(index) ? 1 : 0;
            case ActionType::RING: return startRing(index) ? 1 : 0;
            case ActionType::VISIT: return startVisit(index) ? 1 : 0;
            case ActionType::REBOOT:
            case ActionType::CUT: {
                const int pdn = action.a[0];
                if (pdns[static_cast<size_t>(pdn)].down) return 0;
                if (action.type == ActionType::REBOOT) {
                    takeDown(pdn, false, static_cast<uint64_t>(action.get("down", 1500)));
                } else {
                    pdns[static_cast<size_t>(pdn)].nvs.armCut(
                    static_cast<uint32_t>(action.get("k", 1)), action.get("torn") != 0);
                }
                return 1;
            }
            case ActionType::FDN_OFF: {
                FdnSlot& slot = fdns[static_cast<size_t>(action.b[0])];
                const uint64_t until = now + static_cast<uint64_t>(action.get("dur"));
                if (action.get("mode") & 1) {
                    slot.mutedUntil = std::max(slot.mutedUntil, until);
                    medium.setMuted(slot.mac, true);
                }
                if (action.get("mode") & 2) {
                    slot.offlineUntil = std::max(slot.offlineUntil, until);
                    slot.faults.offline = true;
                }
                return 1;
            }
            case ActionType::FDN_REBOOT: {
                FdnSlot& slot = fdns[static_cast<size_t>(action.b[0])];
                if (slot.down) return 0;
                if (action.get("k") > 0) {
                    slot.nvs.armCut(static_cast<uint32_t>(action.get("k")),
                                    action.get("torn") != 0);
                } else {
                    takeDownFdn(action.b[0], static_cast<uint64_t>(action.get("down", 1500)));
                }
                return 1;
            }
            case ActionType::BURST:
                medium.burstLoss = static_cast<double>(action.get("loss")) / 100.0;
                medium.burstUntil =
                std::max(medium.burstUntil, now + static_cast<uint64_t>(action.get("dur")));
                for (std::pair<const std::string, TruthMatch>& entry : truth) {
                    TruthMatch& match = entry.second;
                    const bool open = !match.hunter.finalized || !match.bounty.finalized;
                    if (open && match.disturbed.empty()) match.disturbed = "burst";
                }
                return 1;
            case ActionType::NO_EVENT:
                // Only the fake can go between events; the real one belongs to the lead.
                if (fake == nullptr) return 1;
                fake->eventActive = false;
                noEventUntil =
                std::max(noEventUntil, now + static_cast<uint64_t>(action.get("dur")));
                return 1;
        }
        return 1;
    }

    Activity& newActivity(size_t index, const std::vector<int>& members, uint64_t deadlineMs) {
        activities.push_back(Activity());
        Activity& activity = activities.back();
        activity.action = index;
        activity.pdns = members;
        activity.deadline = now + deadlineMs;
        for (int pdn : members) {
            PdnSlot& slot = pdns[static_cast<size_t>(pdn)];
            slot.activity = static_cast<int>(index);
            slot.sawCountdown = slot.planned = slot.confirm = slot.clicked = false;
        }
        return activity;
    }

    bool startDuel(size_t index) {
        const Action& action = actions[index];
        std::vector<int> members = action.a;
        members.insert(members.end(), action.b.begin(), action.b.end());
        for (int pdn : members) {
            if (!isReady(pdn)) return false;
        }
        Activity& activity = newActivity(index, members, 45000);
        uint64_t at = now;
        const int64_t confirmMask = action.get("cm");
        int supporter = 0;
        for (const std::vector<int>* side : {&action.a, &action.b}) {
            const bool hunter = side == &action.a;
            for (size_t i = 1; i < side->size(); i++) {
                activity.ops.push_back({at,
                                        true,
                                        {(*side)[i], opponentJack(hunter)},
                                        {(*side)[i - 1], supporterJack(hunter)}});
                pdns[static_cast<size_t>((*side)[i])].confirm = (confirmMask >> supporter++) & 1;
                at += 100;
            }
        }
        if (members.size() > 2) at += 1500;
        const JackRef hunterOut = {action.a[0], SerialIdentifier::OUTPUT_JACK};
        const JackRef bountyIn = {action.b[0], SerialIdentifier::INPUT_JACK};
        for (int64_t i = 0; i < action.get("bounce"); i++) {
            activity.ops.push_back({at, true, hunterOut, bountyIn});
            activity.ops.push_back({at + 40, false, hunterOut, bountyIn});
            at += 80;
        }
        activity.ops.push_back({at, true, hunterOut, bountyIn});
        const int sides[2] = {action.a[0], action.b[0]};
        for (int side = 0; side < 2; side++) {
            PdnSlot& slot = pdns[static_cast<size_t>(sides[side])];
            slot.planned = true;
            slot.reaction = action.get(side == 0 ? "rh" : "rb", 300);
            slot.mash = action.get(side == 0 ? "mh" : "mb");
            if (action.params.count("reboot") && action.get("reboot") == side) {
                slot.rebootDelay = action.get("rebootdelay");
            }
            if (action.params.count("cut") && action.get("cut") == side)
                activity.cutSlot = sides[side];
        }
        return true;
    }

    bool startRing(size_t index) {
        const Action& action = actions[index];
        for (int pdn : action.a) {
            if (!isReady(pdn)) return false;
        }
        Activity& activity = newActivity(index, action.a, 240000);
        for (size_t i = 0; i < action.a.size(); i++) {
            const int next = action.a[(i + 1) % action.a.size()];
            activity.ops.push_back({now + 100 * i,
                                    true,
                                    {action.a[i], SerialIdentifier::OUTPUT_JACK},
                                    {next, SerialIdentifier::INPUT_JACK}});
            pdns[static_cast<size_t>(action.a[i])].confirm = (action.get("cm") >> i) & 1;
        }
        return true;
    }

    bool startVisit(size_t index) {
        const Action& action = actions[index];
        const FdnSlot& fdn = fdns[static_cast<size_t>(action.b[0])];
        const JackRef jacks[2] = {{-action.b[0] - 1, SerialIdentifier::INPUT_JACK},
                                  {-action.b[0] - 1, SerialIdentifier::INPUT_JACK_SECONDARY}};
        const JackRef* free = !cables.isPlugged(jacks[0])   ? &jacks[0]
                              : !cables.isPlugged(jacks[1]) ? &jacks[1]
                                                            : nullptr;
        if (fdn.down || free == nullptr || !isReady(action.a[0])) return false;
        Activity& activity = newActivity(index, action.a, static_cast<uint64_t>(action.get("dur")));
        activity.ops.push_back({now, true, {action.a[0], SerialIdentifier::OUTPUT_JACK}, *free});
        return true;
    }

    // Runs due cable steps; true once the activity is over and unplugged.
    bool updateActivity(Activity& activity) {
        for (size_t i = 0; i < activity.ops.size();) {
            const CableOp op = activity.ops[i];
            if (op.at > now) {
                i++;
                continue;
            }
            activity.ops.erase(activity.ops.begin() + static_cast<long>(i));
            if (!op.plug) {
                cables.unplug(op.a);
            } else if (cables.plug(op.a, op.b)) {
                activity.plugged.push_back({op.a, op.b});
                // The duel cable is the last op: arm a cut on the device about to record.
                if (activity.ops.empty() && activity.cutSlot >= 0) {
                    const Action& action = actions[activity.action];
                    pdns[static_cast<size_t>(activity.cutSlot)].nvs.armCut(
                    static_cast<uint32_t>(action.get("k", 1)), action.get("torn") != 0);
                }
            }
        }
        const Action& action = actions[activity.action];
        bool over = now >= activity.deadline;
        if (action.type == ActionType::DUEL && activity.ops.empty()) {
            over = over || (isSettled(action.a[0]) && isSettled(action.b[0]));
        } else if (action.type == ActionType::RING) {
            bool allSettled = now >= activity.deadline - 240000 + 30000;
            for (int pdn : action.a) {
                const int state = pdns[static_cast<size_t>(pdn)].state;
                allSettled =
                allSettled && (state == SLEEP || state == IDLE || state == AWAKEN_SEQUENCE ||
                               pdns[static_cast<size_t>(pdn)].down);
            }
            over = over || allSettled;
        }
        if (!over) return false;
        for (const std::pair<JackRef, JackRef>& cable : activity.plugged)
            cables.unplug(cable.first);
        for (int pdn : activity.pdns) {
            PdnSlot& slot = pdns[static_cast<size_t>(pdn)];
            slot.activity = -1;
            slot.planned = slot.confirm = false;
            slot.rebootDelay = -1;
        }
        if (activity.cutSlot >= 0 && !pdns[static_cast<size_t>(activity.cutSlot)].down) {
            pdns[static_cast<size_t>(activity.cutSlot)].nvs.armCut(0, false);
        }
        return true;
    }

    bool isSettled(int pdn) const {
        const PdnSlot& slot = pdns[static_cast<size_t>(pdn)];
        if (slot.down || slot.state == WIN || slot.state == LOSE) return true;
        return slot.sawCountdown && (slot.state == IDLE || slot.state == SLEEP);
    }

    // ---- autopilot: the thumbs ----------------------------------------------

    uint64_t mix(uint64_t a, uint64_t b) const {
        uint64_t x = config.schedule.seed * 0x9E3779B97F4A7C15ULL ^ (a << 20) ^ b;
        x ^= x >> 31;
        x *= 0xBF58476D1CE4E5B9ULL;
        return x ^ (x >> 29);
    }

    void autopilot(int index) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        const int state = slot.device->stateId();
        MatchManager* matchManager = slot.device->context().matchManager;
        if (state != slot.state) {
            slot.state = state;
            slot.stateSince = now;
            onEnter(index, state);
        }
        const unsigned long ms = slot.clock->milliseconds();
        if (state == MATCH_COUNTDOWN && slot.mashDone < slot.mash &&
            now - slot.stateSince >= static_cast<uint64_t>(200 * (slot.mashDone + 1))) {
            slot.mashDone++;
            slot.device->press();
        } else if ((state == MATCH || state == MATCH_RECEIVED_RESULT) &&
                   !matchManager->getHasPressedButton() && slot.reaction >= 0 &&
                   ms - slot.matchStart >= static_cast<unsigned long>(slot.reaction)) {
            recordPress(index, static_cast<uint32_t>(ms - slot.matchStart + 75 * slot.mashDone));
            slot.device->press();
        } else if (state == SUPPORTER_READY && slot.confirm) {
            slot.device->press();
        } else if (state == SHOOTOUT_PROPOSAL && slot.confirm && !slot.clicked &&
                   now - slot.stateSince >= 400) {
            slot.clicked = true;
            slot.device->click();
        }
    }

    void onEnter(int index, int state) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        MatchManager* matchManager = slot.device->context().matchManager;
        if (state == MATCH_COUNTDOWN) {
            slot.sawCountdown = true;
            slot.mashDone = 0;
            if (!slot.planned) {
                const uint64_t h = mix(static_cast<uint64_t>(index), ++slot.matchOrdinal);
                slot.reaction = h % 20 == 0 ? -1 : static_cast<int64_t>(150 + h % 450);
                slot.mash = 0;
            }
            slot.planned = false;
        } else if (state == MATCH && matchManager->getCurrentMatch().has_value()) {
            slot.matchId = canonicalId(matchManager->getCurrentMatch()->getMatchId());
            slot.matchStart = matchManager->getMatchLocalStartTime();
            TruthMatch& match = truth[slot.matchId];
            match.shootout = slot.matchId.rfind(kShootoutMatchIdPrefix, 0) == 0;
            if (match.shootout) {
                UUIDBytes stored;
                IdGenerator::uuidStringToBytes(slot.matchId, stored.bytes);
                shootoutIds.insert(formatUuid(stored));
            }
            (slot.identity.hunter ? match.hunter : match.bounty).slot = index;
            if (now < medium.burstUntil && match.disturbed.empty()) match.disturbed = "burst";
            if (slot.rebootDelay >= 0)
                slot.rebootAt = now + static_cast<uint64_t>(slot.rebootDelay) + 1;
            note("draw pdn=" + std::to_string(index) + " id=" + slot.matchId);
        } else if (state == MATCH_RESULT && !slot.matchId.empty()) {
            onFinalize(index);
        }
    }

    std::string describe(const std::string& id) const {
        if (shootoutIds.count(id) != 0) return " (shootout)";
        return legacyIds.count(id) != 0 ? " (older firmware)" : "";
    }

    void recordPress(int index, uint32_t raw) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        TruthMatch& match = truth[slot.matchId];
        Side& side = slot.identity.hunter ? match.hunter : match.bounty;
        side.pressed = true;
        side.raw = raw;
        side.boost = static_cast<uint32_t>(
        std::min(slot.device->context().posseManager->getBoostMs(), PosseManager::getMaxBoostMs()));
        if (slot.activity < 0 ||
            actions[static_cast<size_t>(slot.activity)].type != ActionType::DUEL)
            return;
        // What the scheduler meant: each supporter told to confirm adds 15 ms. The
        // firmware's own count decides the winner; a gap means a confirm was lost or late.
        const Action& action = actions[static_cast<size_t>(slot.activity)];
        const std::vector<int>& team = action.a[0] == index ? action.a : action.b;
        unsigned long planned = 0;
        for (size_t i = 1; i < team.size(); i++) {
            planned +=
            pdns[static_cast<size_t>(team[i])].confirm ? PosseManager::BOOST_PER_SUPPORTER_MS : 0;
        }
        counts[std::min(planned, PosseManager::getMaxBoostMs()) == side.boost ? "boostAsPlanned"
                                                                              : "boostOffPlan"]++;
    }

    // The device mounted MatchResult, so finalizeMatch ran this tick.
    void onFinalize(int index) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        TruthMatch& match = truth[slot.matchId];
        (slot.identity.hunter ? match.hunter : match.bounty).finalized = true;
        const std::string id = slot.matchId;
        slot.matchId.clear();
        if (match.shootout || slot.committedThisTick == id) return;
        const uint32_t seq = slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0);
        const uint32_t collected = slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", 0);
        // Allowed: the ring refuses a match while 32 wait for an FDN (match-manager.cpp:348-355,
        // spec "full ring refuses"); a power cut before "seq" is written loses the match by design
        // (seq moves last, match-manager.cpp:366-371).
        if (seq - std::min(collected, seq) >= MatchManager::MATCH_RING_CAPACITY) {
            counts["allowedRingFull"]++;
        } else if (slot.nvs.isFrozen()) {
            counts["allowedCutBeforeCommit"]++;
        } else {
            fail("pdn " + std::to_string(index) + " finalized " + id + " but stored nothing");
        }
    }

    void onPdnWrite(int index, const std::string& ns, const std::string& key) {
        if (ns != MATCH_RING_PREFS_NAMESPACE || key != "seq") return;
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        const uint32_t seq = slot.nvs.getUInt(ns, key, 0);
        std::string bytes;
        Commit commit{};
        commit.slot = index;
        if (!slot.nvs.get(ns,
                          "match_" + std::to_string((seq - 1) % MatchManager::MATCH_RING_CAPACITY),
                          'b', bytes) ||
            bytes.size() != sizeof(MatchRecord)) {
            fail("pdn " + std::to_string(index) + " committed seq " + std::to_string(seq) +
                 " over an unreadable slot");
            return;
        }
        memcpy(&commit.record, bytes.data(), sizeof(MatchRecord));
        commit.id = formatUuid(commit.record.matchUuid);
        commit.synced = slot.clock->synced;
        commit.trueUnixMs = EVENT_EPOCH_MS + static_cast<int64_t>(now);
        commits.push_back(commit);
        slot.committedThisTick = commit.id;
        note("commit pdn=" + std::to_string(index) + " seq=" + std::to_string(seq) +
             " id=" + commit.id);
    }

    // ---- drain and checks ----------------------------------------------------

    void startDrain() {
        note("drain");
        draining = true;
        medium.burstUntil = 0;
        if (fake != nullptr) fake->eventActive = true;
        noEventUntil = 0;
        for (FdnSlot& slot : fdns) {
            slot.mutedUntil = slot.offlineUntil = 0;
            // Online, but the server still errs: the uploader must keep and retry.
            slot.faults = config.http;
            slot.faults.offline = false;
            if (!slot.down) medium.setMuted(slot.mac, false);
        }
        for (PdnSlot& slot : pdns)
            slot.nvs.armCut(0, false);
        for (FdnSlot& slot : fdns)
            slot.nvs.armCut(0, false);
    }

    bool isQuiescent() const {
        for (const PdnSlot& slot : pdns) {
            if (slot.down || slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0) !=
                             slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", 0)) {
                return false;
            }
        }
        for (const FdnSlot& slot : fdns) {
            if (slot.down || slot.nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_head", 0) !=
                             slot.nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_tail", 0)) {
                return false;
            }
        }
        return true;
    }

    std::string identityOf(int slot, bool team) const {
        if (slot < 0) return "";
        const PdnIdentity& identity = pdns[static_cast<size_t>(slot)].identity;
        return formatUuid(team ? identity.team.id : identity.playerId);
    }

    void checkCommit(const Commit& commit) {
        const std::string where = "pdn " + std::to_string(commit.slot) + " record " + commit.id;
        if (shootoutIds.count(commit.id) != 0) {
            fail(where + ": shootout match stored");
            return;
        }
        std::map<std::string, TruthMatch>::const_iterator found = truth.find(commit.id);
        if (found == truth.end()) {
            fail(where + ": no duel had this id");
            return;
        }
        const TruthMatch& match = found->second;
        const MatchRecord& record = commit.record;
        const Side* sides[2] = {&match.hunter, &match.bounty};
        const UUIDBytes* players[2] = {&record.hunterPlayerId, &record.bountyPlayerId};
        const UUIDBytes* teams[2] = {&record.hunterTeamId, &record.bountyTeamId};
        const uint32_t times[2] = {record.hunterDrawTimeMs, record.bountyDrawTimeMs};
        const bool clean =
        match.disturbed.empty() && match.hunter.finalized && match.bounty.finalized;
        for (int i = 0; i < 2; i++) {
            if (sides[i]->slot < 0) continue;
            if (formatUuid(*players[i]) != identityOf(sides[i]->slot, false) ||
                formatUuid(*teams[i]) != identityOf(sides[i]->slot, true)) {
                fail(where + ": wrong player or team in slot " + std::to_string(i));
            }
            if (sides[i]->pressed && times[i] != sides[i]->raw &&
                (clean || commit.slot == sides[i]->slot)) {
                fail(where + ": raw time " + std::to_string(times[i]) + " != pressed " +
                     std::to_string(sides[i]->raw));
            }
            if (!sides[i]->pressed && clean && times[i] < PITY_FLOOR_MS) {
                fail(where + ": never-pressed side has time " + std::to_string(times[i]));
            }
        }
        if (clean) {
            bool hunterWins = false;
            if (match.hunter.pressed && match.bounty.pressed) {
                hunterWins = static_cast<int64_t>(match.hunter.raw) - match.hunter.boost <
                             static_cast<int64_t>(match.bounty.raw) - match.bounty.boost;
            } else {
                hunterWins = match.hunter.pressed;
            }
            if (record.winnerPlayerId !=
                (hunterWins ? record.hunterPlayerId : record.bountyPlayerId)) {
                fail(where + ": winner disagrees with raw - boost");
            }
            counts["cleanCopiesChecked"]++;
        } else {
            counts["disturbedCopies"]++;
        }
        if (!commit.synced) {
            counts["knownUnsyncedStamp518"]++;
        } else if (std::llabs(record.occurredAtUnixMs - commit.trueUnixMs) >
                   OCCURRED_AT_BOUND_MS + static_cast<int64_t>(now / 1000 * MAX_DRIFT_PPM / 1000)) {
            fail(where + ": occurred-at off by " +
                 std::to_string(record.occurredAtUnixMs - commit.trueUnixMs) + " ms");
        }
    }

    static bool rowMatches(const ServerRow& row, const MatchRecord& record) {
        return row.playerA == formatUuid(record.hunterPlayerId) &&
               row.playerB == formatUuid(record.bountyPlayerId) &&
               row.teamA == formatUuid(record.hunterTeamId) &&
               row.teamB == formatUuid(record.bountyTeamId) &&
               row.winner == formatUuid(record.winnerPlayerId) &&
               row.drawA == record.hunterDrawTimeMs && row.drawB == record.bountyDrawTimeMs &&
               row.occurredAtMs == record.occurredAtUnixMs;
    }

    void checkServer() {
        std::vector<ServerRow> rows;
        std::map<std::string, int> reports;
        if (!server->listMatches(rows) || !server->countReports(reports)) {
            fail("could not read the server");
            return;
        }
        std::map<std::string, std::vector<const Commit*>> copies;
        for (const Commit& commit : commits)
            copies[commit.id].push_back(&commit);
        std::map<std::string, const ServerRow*> byId;
        for (const ServerRow& row : rows) {
            if (byId.count(row.id)) fail("server holds " + row.id + " twice");
            byId[row.id] = &row;
            if (copies.count(row.id) == 0 || shootoutIds.count(row.id) != 0) {
                fail("server row " + row.id + describe(row.id) + " matches no stored duel");
            }
        }
        for (const std::pair<const std::string, std::vector<const Commit*>>& entry : copies) {
            std::map<std::string, const ServerRow*>::const_iterator row = byId.find(entry.first);
            if (row == byId.end()) {
                fail("stored match " + entry.first + " never reached the server");
                continue;
            }
            bool same = false;
            for (const Commit* commit : entry.second)
                same = same || rowMatches(*row->second, commit->record);
            if (!same) fail("server row " + entry.first + " differs from every stored copy");
            const int extra = reports[entry.first] - static_cast<int>(entry.second.size());
            if (extra > 0) counts["extraReports"] += static_cast<uint64_t>(extra);
        }
        for (const std::string& id : airIds) {
            if (copies.count(id) == 0 || shootoutIds.count(id) != 0) {
                fail("relayed " + id + describe(id) + " was never stored as a duel");
            }
        }
        counts["serverRows"] = rows.size();
        counts["storedMatches"] = copies.size();
    }

    void checkInvariants(bool drained) {
        uint32_t highestSeq = 0;
        for (size_t i = 0; i < pdns.size(); i++) {
            const PdnSlot& slot = pdns[i];
            const uint32_t seq = slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0);
            const uint32_t collected =
            slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", 0);
            highestSeq = std::max(highestSeq, seq);
            if (collected > seq || seq - collected > MatchManager::MATCH_RING_CAPACITY) {
                fail("pdn " + std::to_string(i) + " ring marks seq=" + std::to_string(seq) +
                     " collected=" + std::to_string(collected));
            }
            if (!drained && collected != seq) {
                fail("pdn " + std::to_string(i) + " still holds " +
                     std::to_string(seq - collected) + " records");
            }
            if (slot.nvs.snapshot(LEGACY_MATCHES_NAMESPACE_NAME) != slot.legacy) {
                fail("pdn " + std::to_string(i) + " older-firmware matches changed");
            }
        }
        uint32_t highestTail = 0;
        for (size_t i = 0; i < fdns.size(); i++) {
            const uint32_t head = fdns[i].nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_head", 0);
            const uint32_t tail = fdns[i].nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_tail", 0);
            highestTail = std::max(highestTail, tail);
            if (head > tail || tail - head > FDN_MATCH_RECORD_CAPACITY) {
                fail("fdn " + std::to_string(i) + " store head=" + std::to_string(head) +
                     " tail=" + std::to_string(tail));
            }
        }
        counts["maxPdnSeq"] = highestSeq;
        counts["maxFdnTail"] = highestTail;
        if (SimNvsDriver::unopenedNamespaceCalls != 0)
            fail("storage call on an unopened namespace");
    }

    RunResult finish(bool drained) {
        if (ready) {
            for (const Commit& commit : commits)
                checkCommit(commit);
            checkServer();
            checkInvariants(drained);
            if (!drained) fail("drain did not quiesce within the cap");
        }
        counts["commits"] = commits.size();
        counts["framesSent"] = medium.framesSent;
        counts["framesLost"] = medium.framesLost;
        counts["virtualSeconds"] = now / 1000;
        size_t duels = 0;
        for (const std::pair<const std::string, TruthMatch>& entry : truth)
            duels += entry.second.shootout ? 0 : 1;
        counts["duels"] = duels;
        counts["shootoutMatches"] = truth.size() - duels;
        RunResult result;
        result.passed = failures.empty();
        result.failures = failures;
        // On failure, each device's last warnings and errors follow the event log.
        for (size_t i = 0; i < pdns.size() && !failures.empty(); i++) {
            for (const std::string& line : pdns[i].logger.lines)
                note("pdn=" + std::to_string(i) + " " + line);
        }
        for (size_t i = 0; i < fdns.size() && !failures.empty(); i++) {
            for (const std::string& line : fdns[i].logger.lines)
                note("fdn=" + std::to_string(i) + " " + line);
        }
        result.log = log;
        result.logHash = fnv1a(log);
        result.counts = counts;
        char head[160];
        snprintf(head, sizeof(head),
                 "seed=%llu pdns=%d fdns=%d actions=%zu %s fails=%zu hash=%016llx",
                 static_cast<unsigned long long>(config.schedule.seed), config.schedule.pdns,
                 config.schedule.fdns, actions.size(), result.passed ? "PASS" : "FAIL",
                 failures.size(), static_cast<unsigned long long>(result.logHash));
        result.summary = head;
        for (const std::pair<const std::string, uint64_t>& count : counts) {
            result.summary += " " + count.first + "=" + std::to_string(count.second);
        }
        return result;
    }

    SoakConfig config;
    std::vector<Action> actions;
    uint64_t now = 0;
    VirtualMedium medium;
    VirtualCables cables;
    EventServer* server = nullptr;
    FakeServer* fake = nullptr;
    uint64_t noEventUntil = 0;
    std::vector<PdnSlot> pdns;
    std::vector<FdnSlot> fdns;
    std::vector<Activity> activities;
    size_t nextAction = 0;
    std::vector<size_t> deferred;
    std::map<std::string, TruthMatch> truth;
    std::vector<Commit> commits;
    std::set<std::string> airIds;
    std::set<std::string> legacyIds;
    // Shootout ids as a stored record would carry them (uuidStringToBytes of "SHT-...").
    std::set<std::string> shootoutIds;
    std::vector<std::string> failures;
    std::map<std::string, uint64_t> counts;
    std::string log;
    PlatformClock* savedClock = nullptr;
    LoggerInterface* savedLogger = nullptr;
    bool ready = false;
    bool draining = false;
};

/** Generates the seed's actions and plays them. */
inline RunResult runSeed(SoakConfig config) {
    World world(config, Scheduler::generate(config.schedule));
    return world.run();
}

}  // namespace soak
