#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "id-generator.hpp"

#include "sim-devices.hpp"
#include "soak-actions.hpp"
#include "soak-hooks.hpp"

// soak-hooks.hpp defines extern "C" symbols: include this header from exactly one
// translation unit per test binary.

namespace soak {

inline constexpr uint64_t TICK_MS = 5;
inline constexpr uint64_t HELLO_CADENCE_MS = 20;
// How long a reboot or cut keeps a device down when its action draws no "down".
inline constexpr uint64_t DEFAULT_DOWN_MS = 1500;
// The event's wall clock at virtual time zero on the fake server (2026-11-18).
inline constexpr int64_t EVENT_EPOCH_MS = 1795000000000LL;
// A PDN's clock runs up to this fast or slow; the FDN's own clock is exact.
inline constexpr int32_t MAX_DRIFT_PPM = 100;

struct SoakConfig {
    uint64_t seed = 1;
    int pdns = 8;
    int fdns = 2;
    MediumConfig medium;
    ScheduleConfig schedule;
    HttpFaults http;
};

struct RunResult {
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

// One simulated event: PDNs and FDNs on one medium, cables between their jacks,
// and the server their uploads reach. PDN slot i is jack device i; FDN slot i is
// jack device -1 - i. Actions start activities (cable plugs, presses through the
// autopilot) and inject faults; every step is written to a log whose hash
// identifies the run.
class World {
public:
    /** Builds the event for @p config and will play @p actions. */
    World(const SoakConfig& config, const std::vector<Action>& actions)
        : config(config)
        , actions(actions)
        , medium(config.seed ^ 0x11, now)
        , cables([this](const JackRef& jack) { return resolveJack(jack); }) {
        medium.config = config.medium;
    }

    /** Not copyable: devices hold references into the world. */
    World(const World&) = delete;
    /** Not assignable. */
    World& operator=(const World&) = delete;

    /** Frees every device and restores the clock and entropy it found. */
    ~World() {
        destroyAll(pdns);
        destroyAll(fdns);
        delete server;
        SimpleTimer::setPlatformClock(savedClock);
        releaseEntropy();
    }

    /** Plays the actions until the horizon and every activity has ended. */
    RunResult run() {
        setUp();
        const uint64_t horizon = config.schedule.horizonMs;
        while (now < horizon || nextAction < actions.size() || !deferred.empty())
            tick();
        while (!activities.empty() && now < horizon + 300000)
            tick();
        return finish();
    }

protected:
    template <class Device> struct Slot {
        Device* device = nullptr;
        SimNvs nvs;
        VirtualClock* clock = nullptr;
        uint8_t mac[6] = {};
        bool down = false;
        uint64_t upAt = 0;
        // How long the armed cut keeps the device down once it fires.
        uint64_t cutDownMs = DEFAULT_DOWN_MS;
    };
    struct PdnSlot : Slot<SimPdn> {
        PdnIdentity identity;
        bool powerLost = false;
        int activity = -1;
        int state = -1;
        uint64_t stateSince = 0;
        bool sawCountdown = false;
        std::string matchId;
        // This PDN's clock at the tick the harness saw it enter MATCH.
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
    };
    struct FdnSlot : Slot<SimFdn> {
        HttpFaults faults;
        std::mt19937_64 rng;
        uint64_t mutedUntil = 0;
        uint64_t offlineUntil = 0;
        uint64_t httpErrorUntil = 0;
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
        uint64_t startedAt = 0;
    };

    template <class S> void destroyAll(std::vector<S>& slots) {
        for (S& slot : slots) {
            enter(slot);
            delete slot.device;
            delete slot.clock;
        }
    }

    // Each device runs with its own clock installed.
    template <class S> static void enter(S& slot) { SimpleTimer::setPlatformClock(slot.clock); }

    void note(const std::string& line) { log += "t=" + std::to_string(now) + " " + line + "\n"; }

    // Registers two teams and a player per PDN (hunters on even slots), then boots
    // every device. Match ids and seqIds come from the seeded entropy.
    void setUp() {
        const uint64_t seed = config.seed;
        server = new FakeServer(seed ^ 0x22);
        seedEntropy(seed);
        IdGenerator::initialize(static_cast<unsigned long>(seed));
        srand(static_cast<unsigned>(seed));
        std::mt19937_64 rng(seed ^ 0x33);
        Team teams[2];
        strncpy(teams[0].name, "Hunter", sizeof(teams[0].name) - 1);
        strncpy(teams[1].name, "Bounty", sizeof(teams[1].name) - 1);
        server->addTeam("Hunter", teams[0].id);
        server->addTeam("Bounty", teams[1].id);
        pdns.resize(static_cast<size_t>(config.pdns));
        for (size_t i = 0; i < pdns.size(); i++) {
            PdnSlot& slot = pdns[i];
            const uint8_t mac[6] = {
            0x02, 0x50, 0, 0, static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
            memcpy(slot.mac, mac, 6);
            slot.identity.team = teams[i % 2];
            server->addPlayer("pdn-" + std::to_string(i), slot.identity.team.id,
                              slot.identity.playerId);
            const int32_t drift =
            static_cast<int32_t>(rng() % (2 * MAX_DRIFT_PPM + 1)) - MAX_DRIFT_PPM;
            slot.clock = new VirtualClock(now, drift);
            const int index = static_cast<int>(i);
            slot.nvs.onCut = [this, index](const std::string&, const std::string&) {
                medium.setMuted(pdns[index].mac, true);
            };
            bootPdn(index);
        }
        fdns.resize(static_cast<size_t>(config.fdns));
        for (size_t i = 0; i < fdns.size(); i++) {
            FdnSlot& slot = fdns[i];
            const uint8_t mac[6] = {0x02, 0xFD, 0, 0, 0, static_cast<uint8_t>(i)};
            memcpy(slot.mac, mac, 6);
            slot.rng.seed(seed ^ (0x44 + i));
            slot.faults = config.http;
            slot.clock = new VirtualClock(now, 0);
            slot.nvs.onCut = [this, i](const std::string&, const std::string&) {
                medium.setMuted(fdns[i].mac, true);
            };
            bootFdn(static_cast<int>(i));
        }
        for (const Action& action : actions)
            note("plan " + formatAction(action));
    }

    void bootPdn(int index) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        slot.clock->boot(slot.powerLost);
        slot.nvs.thaw();
        medium.setMuted(slot.mac, false);
        enter(slot);
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
        enter(slot);
        slot.device = new SimFdn("_f" + std::to_string(index), medium, slot.mac, slot.nvs, *server,
                                 slot.faults, slot.rng);
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

    // One step of virtual time: the air, the cables, the actions, then every device
    // in slot order, each PDN followed by its thumbs.
    void tick() {
        now += TICK_MS;
        medium.deliverDue();
        if (ends(noEventUntil)) server->eventActive = true;
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
            step(slot, hello);
            autopilot(static_cast<int>(i));
            const bool rebootDue = slot.rebootAt != 0 && now >= slot.rebootAt;
            if (rebootDue && !slot.nvs.isFrozen()) counts["duelRebootsFired"]++;
            if (slot.nvs.isFrozen() || rebootDue) {
                takeDown(static_cast<int>(i), slot.nvs.isFrozen(),
                         slot.nvs.isFrozen() ? slot.cutDownMs : 0);
            }
        }
        for (size_t i = 0; i < fdns.size(); i++) {
            FdnSlot& slot = fdns[i];
            if (slot.down) {
                if (now >= slot.upAt) bootFdn(static_cast<int>(i));
                continue;
            }
            if (ends(slot.mutedUntil)) medium.setMuted(slot.mac, false);
            if (ends(slot.offlineUntil)) slot.faults.offline = config.http.offline;
            if (ends(slot.httpErrorUntil)) {
                slot.faults.serverError = config.http.serverError;
                slot.faults.transportFail = config.http.transportFail;
            }
            step(slot, hello);
            if (slot.nvs.isFrozen()) takeDownFdn(static_cast<int>(i), slot.cutDownMs);
        }
    }

    template <class S> static void step(S& slot, bool hello) {
        enter(slot);
        if (hello) slot.device->firmware->getRemoteDeviceCoordinator()->emitHello();
        slot.device->firmware->loop();
    }

    // True once, on the first tick at or past a fault window's end; the window is then closed.
    bool ends(uint64_t& until) const {
        if (until == 0 || now < until) return false;
        until = 0;
        return true;
    }

    // Keeps a fault window open at least the action's "dur" from now.
    void extend(uint64_t& until, const Action& action) const {
        until = std::max(until, now + static_cast<uint64_t>(action.get("dur")));
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

    // The device is gone for @p downMs and silent on the air meanwhile.
    template <class S> void powerOff(S& slot, uint64_t downMs) {
        enter(slot);
        delete slot.device;
        slot.device = nullptr;
        slot.down = true;
        slot.upAt = now + downMs;
        medium.setMuted(slot.mac, true);
    }

    // Arms the action's power cut ("k", "torn"); it keeps the device down for "down" ms.
    template <class S> static void armCut(S& slot, const Action& action) {
        slot.nvs.armCut(static_cast<uint32_t>(action.get("k", 1)), action.get("torn") != 0);
        slot.cutDownMs = static_cast<uint64_t>(action.get("down", DEFAULT_DOWN_MS));
    }

    // A reboot (soft) or a power cut: the PDN is gone for @p downMs (the default when 0).
    void takeDown(int index, bool powerLost, uint64_t downMs) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        if (slot.down) return;
        powerOff(slot, downMs != 0 ? downMs : DEFAULT_DOWN_MS);
        slot.powerLost = powerLost;
        slot.rebootAt = 0;
        slot.rebootDelay = -1;
        const char* cause = powerLost ? "cut" : "reboot";
        counts[powerLost ? "pdnCutsTakenDown" : "pdnRebootsFired"]++;
        note(std::string(cause) + " pdn=" + std::to_string(index));
    }

    void takeDownFdn(int index, uint64_t downMs) {
        FdnSlot& slot = fdns[static_cast<size_t>(index)];
        if (slot.down) return;
        powerOff(slot, downMs);
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

    // Due actions wait up to 30 s for their devices; then they count as skipped.
    void applyActions() {
        while (nextAction < actions.size() && actions[nextAction].at <= now) {
            deferred.push_back(nextAction++);
        }
        for (size_t i = 0; i < deferred.size();) {
            const Action& action = actions[deferred[i]];
            const bool applied = tryApply(deferred[i]);
            if (!applied && now < action.at + 30000) {
                i++;
                continue;
            }
            const std::string verdict = applied ? "apply" : "skip";
            note(verdict + " " + std::to_string(deferred[i]));
            counts[applied ? "actionsApplied" : "actionsSkipped"]++;
            counts[verdict + "_" + ACTION_NAMES[static_cast<int>(action.type)]]++;
            deferred.erase(deferred.begin() + static_cast<long>(i));
        }
    }

    // True once applied; false while not yet possible.
    bool tryApply(size_t index) {
        const Action& action = actions[index];
        switch (action.type) {
            case ActionType::DUEL: return startDuel(index);
            case ActionType::RING: return startRing(index);
            case ActionType::VISIT: return startVisit(index);
            case ActionType::REBOOT:
            case ActionType::CUT: {
                const int pdn = action.a[0];
                if (pdns[static_cast<size_t>(pdn)].down) return false;
                if (action.type == ActionType::REBOOT) {
                    takeDown(pdn, false,
                             static_cast<uint64_t>(action.get("down", DEFAULT_DOWN_MS)));
                } else {
                    armCut(pdns[static_cast<size_t>(pdn)], action);
                }
                return true;
            }
            case ActionType::FDN_OFF: {
                FdnSlot& slot = fdns[static_cast<size_t>(action.b[0])];
                if (action.get("mode") & 1) {
                    extend(slot.mutedUntil, action);
                    medium.setMuted(slot.mac, true);
                }
                if (action.get("mode") & 2) {
                    extend(slot.offlineUntil, action);
                    slot.faults.offline = true;
                }
                return true;
            }
            case ActionType::FDN_REBOOT: {
                FdnSlot& slot = fdns[static_cast<size_t>(action.b[0])];
                if (slot.down) return false;
                if (action.get("k") > 0) {
                    armCut(slot, action);
                } else {
                    takeDownFdn(action.b[0],
                                static_cast<uint64_t>(action.get("down", DEFAULT_DOWN_MS)));
                }
                return true;
            }
            case ActionType::BURST:
                medium.burstLoss = static_cast<double>(action.get("loss")) / 100.0;
                extend(medium.burstUntil, action);
                return true;
            case ActionType::NO_EVENT:
                server->eventActive = false;
                extend(noEventUntil, action);
                return true;
            case ActionType::HTTP_ERROR: {
                FdnSlot& slot = fdns[static_cast<size_t>(action.b[0])];
                extend(slot.httpErrorUntil, action);
                (action.get("mode") == 2 ? slot.faults.transportFail : slot.faults.serverError) = 1;
                return true;
            }
            case ActionType::ACK_LOSS: extend(medium.dropAcksUntil, action); return true;
        }
        return true;
    }

    Activity& newActivity(size_t index, const std::vector<int>& members, uint64_t deadlineMs) {
        activities.push_back(Activity());
        Activity& activity = activities.back();
        activity.action = index;
        activity.pdns = members;
        activity.deadline = now + deadlineMs;
        activity.startedAt = now;
        for (int pdn : members) {
            PdnSlot& slot = pdns[static_cast<size_t>(pdn)];
            slot.activity = static_cast<int>(index);
            slot.sawCountdown = slot.planned = slot.confirm = slot.clicked = false;
        }
        return activity;
    }

    // Supporters chain in first (posse cables, then their confirm presses), then the
    // duel cable goes in, after optional bounces, and the challengers press on the draw.
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
                counts["duelRebootsPlanned"]++;
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

    // A PDN's output jack in a free FDN input jack: the FDN context sets its clock.
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
                    armCut(pdns[static_cast<size_t>(activity.cutSlot)], actions[activity.action]);
                }
            }
        }
        const Action& action = actions[activity.action];
        bool over = now >= activity.deadline;
        if (action.type == ActionType::DUEL && activity.ops.empty()) {
            over = over || (isSettled(action.a[0]) && isSettled(action.b[0]));
        } else if (action.type == ActionType::RING) {
            bool allSettled = now >= activity.startedAt + 30000;
            for (int pdn : action.a) {
                const int state = pdns[static_cast<size_t>(pdn)].state;
                // Final standings wait for the ring to open: the players unplug.
                allSettled =
                allSettled &&
                (state == SLEEP || state == IDLE || state == AWAKEN_SEQUENCE ||
                 state == SHOOTOUT_FINAL_STANDINGS || pdns[static_cast<size_t>(pdn)].down);
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
        uint64_t x = config.seed * 0x9E3779B97F4A7C15ULL ^ (a << 20) ^ b;
        x ^= x >> 31;
        x *= 0xBF58476D1CE4E5B9ULL;
        return x ^ (x >> 29);
    }

    // Presses as the action planned, or as the seed picks for a match nobody planned:
    // mashes in the countdown, the draw, supporter confirms and shootout proposals.
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
            slot.matchId = matchManager->getCurrentMatch()->getMatchId();
            // The draw starts on this tick, by this PDN's own clock.
            slot.matchStart = slot.clock->milliseconds();
            if (slot.rebootDelay >= 0)
                slot.rebootAt = now + static_cast<uint64_t>(slot.rebootDelay) + 1;
            note("draw pdn=" + std::to_string(index) + " id=" + slot.matchId);
        }
    }

    template <class S> void countCuts(const std::vector<S>& slots) {
        for (const S& slot : slots) {
            counts["cutsArmed"] += slot.nvs.cutsArmed;
            counts["cutsFired"] += slot.nvs.cutsFired;
        }
    }

    // The log, its hash, and the fault counts: armed cuts against fired ones.
    RunResult finish() {
        countCuts(pdns);
        countCuts(fdns);
        counts["cutsArmedNeverFired"] = counts["cutsArmed"] - counts["cutsFired"];
        counts["duelRebootsNeverFired"] = counts["duelRebootsPlanned"] - counts["duelRebootsFired"];
        RunResult result;
        result.log = log;
        result.logHash = fnv1a(log);
        result.counts = counts;
        return result;
    }

    SoakConfig config;
    std::vector<Action> actions;
    uint64_t now = 0;
    VirtualMedium medium;
    VirtualCables cables;
    FakeServer* server = nullptr;
    uint64_t noEventUntil = 0;
    std::vector<PdnSlot> pdns;
    std::vector<FdnSlot> fdns;
    std::vector<Activity> activities;
    size_t nextAction = 0;
    std::vector<size_t> deferred;
    std::map<std::string, uint64_t> counts;
    std::string log;
    PlatformClock* savedClock = SimpleTimer::getPlatformClock();
};

/** Generates the seed's actions and plays them. */
inline RunResult runSeed(const SoakConfig& config) {
    World world(config,
                Scheduler::generate(config.schedule, config.seed, config.pdns, config.fdns));
    return world.run();
}

}  // namespace soak
