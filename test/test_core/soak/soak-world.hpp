#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
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
// The event's wall clock at virtual time zero on the fake server (2026-11-18). A real
// server run takes the real wall clock instead, so its scoring period covers the stamps.
inline constexpr int64_t EVENT_EPOCH_MS = 1795000000000LL;
// A stamp this far before the event's epoch is uptime, not UTC: the PDN's clock was not set.
inline constexpr int64_t UPTIME_STAMP_BEFORE_MS = 86400000LL;
// A PDN's chain frames kept for the boost check: far longer than one duel.
inline constexpr uint64_t HEARD_KEPT_MS = 120000;
// A synced PDN stamps occurred-at from a clock set by an FDN context that crossed
// the medium. The context carries the FDN's time when it was first sent; the
// Resender retransmits that same payload for up to RETRANSMIT_SPAN_MS, the medium
// adds its delay and reorder, and the PDN reads it on its next tick. From the sync
// on, the PDN's drift (at most MAX_DRIFT_PPM) accrues. The FDN's own clock is exact.
inline constexpr int32_t MAX_DRIFT_PPM = 100;
inline constexpr uint32_t PITY_FLOOR_MS = 750;
// How long after its first draw a duel can still be resolving: the 4 s draw window,
// the 2.5 s post-push grace, and room for the two sides' countdowns to differ. A fault
// later than this cannot explain a side that never finalized.
inline constexpr uint64_t MATCH_OPEN_MS = 10000;

/** SplitMix64's finalizer: every input bit reaches every output bit. */
inline uint64_t splitMix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

struct SoakConfig {
    ScheduleConfig schedule;
    MediumConfig medium;
    HttpFaults http;
    uint64_t drainCapMs = 40 * 60000;
    // Empty: the in-process fake server. Else a loopback alleycat-rs.
    std::string serverUrl;
    // The run's entropy nonce, printed on the summary line. Unset: 0 on the fake
    // server, a fresh one on a real server. Set to a real run's nonce to replay that
    // run's match ids and seqIds on the fake server; on a real server a reused nonce
    // re-sends ids the database already holds under another event (409).
    std::optional<uint64_t> nonce;
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
        destroyAll(pdns);
        destroyAll(fdns);
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
    template <class Device> struct Slot {
        Device* device = nullptr;
        SimNvs nvs;
        VirtualClock* clock = nullptr;
        SoakLogger logger;
        uint8_t mac[6] = {};
        bool down = false;
        uint64_t upAt = 0;
    };
    // A posse frame that reached a PDN's radio: a supporter's confirm, a supporter's
    // join naming this PDN, or a challenger's countdown. `from` is the PDN it speaks
    // for (the confirm's originator, the joiner, the countdown's challenger).
    struct Heard {
        uint64_t at;
        PktType type;
        int from;
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
        std::string committedThisTick;
        std::map<std::string, std::string> legacy;
        // The tick of its last MATCH_COUNTDOWN entry: a challenger's roll call starts empty there.
        uint64_t countdownAt = 0;
        // Its first press on SUPPORTER_READY in the current activity.
        bool supporterPressed = false;
        uint64_t supporterPressAt = 0;
        // The last time it went down (reboot or cut); -1 never.
        int64_t downAt = -1;
        // Its full ring refused a match; the next commit shows it stores again.
        bool ringRefused = false;
        std::vector<Heard> heard;
    };
    struct FdnSlot : Slot<SimFdn> {
        HttpFaults faults;
        std::mt19937_64 rng;
        uint64_t mutedUntil = 0;
        uint64_t offlineUntil = 0;
        uint64_t httpErrorUntil = 0;
        // The last record frame seqId per sender, as the FDN's channel dedupes it.
        std::map<uint64_t, uint8_t> lastRecordSeq;
        // The store's tail and fill level just before the frame being handled.
        uint32_t tailBefore = 0;
        uint32_t storedBefore = 0;
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
        // DUEL: the first id the hunter (0) and the bounty (1) drew in this activity.
        bool drew[2] = {false, false};
        std::string drawn[2];
        // Injected faults that touched it: a member's reboot or cut, a loss burst.
        std::set<std::string> causes;
    };
    struct Side {
        int slot = -1;
        bool pressed = false;
        bool finalized = false;
        uint32_t raw = 0;
        // The boost the winner check uses: the harness's own count, or the
        // firmware's when an injected fault explains the difference.
        uint32_t boost = 0;
        // Why its finalize stored nothing, when allowed: ringFull or cutBeforeCommit.
        std::string excuse;
    };
    struct TruthMatch {
        bool shootout = false;
        Side hunter;
        Side bounty;
        // Injected faults while it was open: reboot, cut, burst. Empty = undisturbed.
        std::set<std::string> causes;
        uint64_t drawAt = 0;

        /** True while a side is unfinalized and the draw is recent enough to still resolve. */
        bool isOpen(uint64_t now) const {
            return (!hunter.finalized || !bounty.finalized) && now <= drawAt + MATCH_OPEN_MS;
        }
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

    template <class S> void destroyAll(std::vector<S>& slots) {
        for (S& slot : slots) {
            enter(slot.clock, &slot.logger);
            delete slot.device;
            delete slot.clock;
        }
    }

    void enter(PlatformClock* clock, LoggerInterface* logger) {
        SimpleTimer::setPlatformClock(clock);
        g_logger = logger;
    }

    void note(const std::string& line) { log += "t=" + std::to_string(now) + " " + line + "\n"; }

    void fail(const std::string& message) { failures.push_back(message); }

    bool setUp() {
        const uint64_t seed = config.schedule.seed;
        nonce = 0;
        uint64_t startedAt = 0;
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
            // Match ids must be new to a database that keeps every earlier run: a plain
            // seed ^ seconds repeats across runs (seed 3005 and, 5319 s later, seed 6006
            // drew the same ids, refused as another event's), so hash the start time.
            startedAt = static_cast<uint64_t>(time(nullptr));
            nonce =
            splitMix64(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count()));
        }
        if (config.nonce.has_value()) nonce = *config.nonce;
        seedEntropy(seed ^ nonce);
        IdGenerator::initialize(static_cast<unsigned long>(seed ^ nonce));
        srand(static_cast<unsigned>(seed));
        std::mt19937_64 rng(seed ^ 0x33);
        Team teams[2];
        strncpy(teams[0].name, "Hunter", sizeof(teams[0].name) - 1);
        strncpy(teams[1].name, "Bounty", sizeof(teams[1].name) - 1);
        if (!server->openEvent("soak-" + std::to_string(seed) + "-" + std::to_string(startedAt)) ||
            !server->addTeam("Hunter", teams[0].id) || !server->addTeam("Bounty", teams[1].id)) {
            fail("server set-up failed (is the local server up?)");
            return false;
        }
        // The real server's scoring period opened at its now; stamps start a little later.
        if (fake == nullptr) {
            epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count() +
                      5000;
        } else {
            fake->scoringFromMs = epochMs;
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
            slot.nvs.onCut = [this, index](const std::string&, const std::string&) {
                medium.setMuted(pdns[index].mac, true);
            };
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
            slot.nvs.onCut = [this, i](const std::string& ns, const std::string& key) {
                medium.setMuted(fdns[i].mac, true);
                if (ns == FDN_RECORD_STORE_NAMESPACE && key == "m_head") onFdnHeadCut(i);
            };
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
        medium.onDelivered = [this](const uint8_t* src, const uint8_t* dst, PktType type,
                                    const std::vector<uint8_t>& data) {
            onDelivered(src, dst, type, data);
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
        slot.clock->setUnixMilliseconds(epochMs + static_cast<int64_t>(now));
        slot.nvs.thaw();
        medium.setMuted(slot.mac, now < slot.mutedUntil);
        enter(slot.clock, &slot.logger);
        slot.device =
        new SimFdn("_f" + std::to_string(index), medium, slot.mac, slot.nvs,
                   server->deviceBackend(), slot.faults, slot.rng, unconfirmedUploads);
        slot.lastRecordSeq.clear();
        slot.device->radio->beforeHandled = [this, index](const uint8_t*, PktType) {
            FdnSlot& fdn = fdns[static_cast<size_t>(index)];
            fdn.tailBefore = fdn.nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_tail", 0);
            fdn.storedBefore =
            fdn.tailBefore - fdn.nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_head", 0);
        };
        slot.device->radio->onHandled =
        [this, index](const uint8_t* src, PktType type, const std::vector<uint8_t>& data,
                      bool acked) { onFdnFrame(index, src, type, data, acked); };
        slot.down = false;
        note("up fdn=" + std::to_string(index));
    }

    // An FDN acks every new record frame it stores or already holds (fdn-record-store.hpp
    // store()); it may stay silent only when its ring was full before the frame or the
    // write failed (power cut). A frame it stored must be acked.
    void onFdnFrame(int index, const uint8_t* src, PktType type, const std::vector<uint8_t>& data,
                    bool acked) {
        if (type != PktType::kMatchRecord || data.size() != sizeof(MatchRecordFramePacket)) return;
        FdnSlot& slot = fdns[static_cast<size_t>(index)];
        MatchRecordFramePacket frame;
        memcpy(&frame, data.data(), sizeof(frame));
        uint8_t& last = slot.lastRecordSeq[macKey(src)];
        const bool repeat = frame.seqId != 0 && frame.seqId == last;
        last = frame.seqId;
        const std::string what = "fdn " + std::to_string(index) + " record " +
                                 std::to_string(frame.recordNumber) + " from pdn " +
                                 std::to_string(src[4] << 8 | src[5]);
        if (slot.nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_tail", 0) != slot.tailBefore) {
            MatchRecord record;
            memcpy(&record, data.data() + 6, sizeof(record));
            const std::string id = formatUuid(record.matchUuid);
            // A copy stored a second time, here after a retire or at another FDN, is
            // uploaded again: a resend the server's report count may show.
            const std::string key =
            std::to_string(macKey(src)) + "/" + std::to_string(frame.recordNumber) + "/" + id;
            if (!storedRecords.insert(key).second) {
                resendAllowance[id]++;
                counts["recordsStoredAgain"]++;
            }
            if (!acked && !slot.nvs.isFrozen()) fail(what + " stored without an ack");
            return;
        }
        if (acked || repeat) return;
        if (slot.nvs.isFrozen() || slot.storedBefore >= FDN_MATCH_RECORD_CAPACITY) {
            counts["allowedFdnRefusals"]++;
            return;
        }
        fail(what + " left unacked");
    }

    // The FDN lost its head write: after the reboot it uploads again what it had retired.
    void onFdnHeadCut(size_t index) {
        const SimNvs& nvs = fdns[index].nvs;
        const uint32_t head = nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_head", 0);
        const uint32_t tail = nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_tail", 0);
        for (uint32_t i = head; i != tail; i++) {
            std::string bytes;
            FdnRecordStore::MatchRing::Entry entry;
            if (!nvs.get(FDN_RECORD_STORE_NAMESPACE,
                         "m_" + std::to_string(i % FDN_MATCH_RECORD_CAPACITY), 'b', bytes) ||
                bytes.size() != sizeof(entry)) {
                continue;
            }
            memcpy(&entry, bytes.data(), sizeof(entry));
            MatchRecord record;
            memcpy(&record, entry.content, sizeof(record));
            resendAllowance[formatUuid(record.matchUuid)]++;
        }
        counts["fdnHeadWritesCut"]++;
    }

    /** The PDN slot behind @p mac, or -1. */
    int pdnIndexOf(const uint8_t* mac) const {
        if (mac[0] != 0x02 || mac[1] != 0x50 || mac[2] != 0 || mac[3] != 0) return -1;
        const int index = mac[4] << 8 | mac[5];
        return index < static_cast<int>(pdns.size()) ? index : -1;
    }

    // What each PDN heard of the posse traffic, for the harness's own boost count.
    void onDelivered(const uint8_t* src, const uint8_t* dst, PktType type,
                     const std::vector<uint8_t>& data) {
        const int to = pdnIndexOf(dst);
        if (to < 0) return;
        Heard heard{now, type, -1};
        if (type == PktType::kChainConfirm && data.size() == sizeof(ChainConfirmPayload)) {
            heard.from = pdnIndexOf(data.data());
        } else if (type == PktType::kChainJoin && data.size() == sizeof(ChainJoinPayload) &&
                   memcmp(data.data(), dst, 6) == 0) {
            heard.from = pdnIndexOf(src);
        } else if (type == PktType::kChainGameEvent &&
                   data.size() == sizeof(ChainGameEventPayload) &&
                   data[0] == static_cast<uint8_t>(ChainGameEventType::COUNTDOWN)) {
            heard.from = pdnIndexOf(data.data() + 2);
        }
        if (heard.from < 0) return;
        std::vector<Heard>& list = pdns[static_cast<size_t>(to)].heard;
        while (!list.empty() && list.front().at + HEARD_KEPT_MS < now)
            list.erase(list.begin());
        list.push_back(heard);
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
            const bool rebootDue = slot.rebootAt != 0 && now >= slot.rebootAt;
            if (rebootDue && !slot.nvs.isFrozen()) counts["duelRebootsFired"]++;
            if (slot.nvs.isFrozen() || rebootDue) {
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
            if (slot.httpErrorUntil != 0 && now >= slot.httpErrorUntil) {
                slot.httpErrorUntil = 0;
                slot.faults.serverError = config.http.serverError;
                slot.faults.transportFail = config.http.transportFail;
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
        slot.downAt = static_cast<int64_t>(now);
        medium.setMuted(slot.mac, true);
        const char* cause = powerLost ? "cut" : "reboot";
        for (std::pair<const std::string, TruthMatch>& entry : truth) {
            TruthMatch& match = entry.second;
            const bool principal = match.hunter.slot == index || match.bounty.slot == index;
            if (principal && match.isOpen(now)) match.causes.insert(cause);
        }
        for (Activity& activity : activities) {
            if (std::find(activity.pdns.begin(), activity.pdns.end(), index) != activity.pdns.end())
                activity.causes.insert(cause);
        }
        counts[powerLost ? "pdnCutsTakenDown" : "pdnRebootsFired"]++;
        note(std::string(cause) + " pdn=" + std::to_string(index));
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
            static const char* const VERDICTS[] = {"skip", "apply", "n/a"};
            static const char* const COUNTS[] = {"actionsSkipped", "actionsApplied",
                                                 "actionsNotApplicable"};
            note(std::string(VERDICTS[result]) + " " + std::to_string(deferred[i]));
            counts[COUNTS[result]]++;
            counts[std::string(VERDICTS[result]) + "_" +
                   ACTION_NAMES[static_cast<int>(action.type)]]++;
            deferred.erase(deferred.begin() + static_cast<long>(i));
        }
    }

    // 1 applied, 0 not yet possible, 2 this server cannot do it (never applied).
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
                    if (match.isOpen(now)) match.causes.insert("burst");
                }
                for (Activity& activity : activities)
                    activity.causes.insert("burst");
                return 1;
            case ActionType::NO_EVENT:
                // alleycat-rs at a01a3fd has no route that clears the active event, and
                // selecting another one is not "between events": uploads there are
                // rejected for unknown players and retired. Fake server only.
                if (fake == nullptr) return 2;
                fake->eventActive = false;
                noEventUntil =
                std::max(noEventUntil, now + static_cast<uint64_t>(action.get("dur")));
                return 1;
            case ActionType::HTTP_ERROR: {
                FdnSlot& slot = fdns[static_cast<size_t>(action.b[0])];
                slot.httpErrorUntil =
                std::max(slot.httpErrorUntil, now + static_cast<uint64_t>(action.get("dur")));
                (action.get("mode") == 2 ? slot.faults.transportFail : slot.faults.serverError) = 1;
                return 1;
            }
            case ActionType::ACK_LOSS:
                medium.dropAcksUntil =
                std::max(medium.dropAcksUntil, now + static_cast<uint64_t>(action.get("dur")));
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
        activity.startedAt = now;
        if (now < medium.burstUntil) activity.causes.insert("burst");
        for (int pdn : members) {
            PdnSlot& slot = pdns[static_cast<size_t>(pdn)];
            slot.activity = static_cast<int>(index);
            slot.sawCountdown = slot.planned = slot.confirm = slot.clicked = false;
            slot.supporterPressed = false;
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
                // Final standings wait for the ring to open: the players unplug.
                allSettled =
                allSettled &&
                (state == SLEEP || state == IDLE || state == AWAKEN_SEQUENCE ||
                 state == SHOOTOUT_FINAL_STANDINGS || pdns[static_cast<size_t>(pdn)].down);
            }
            over = over || allSettled;
        }
        if (!over) return false;
        if (action.type == ActionType::DUEL) checkDuelDraws(activity);
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

    // Both principals of a DUEL must draw the same id; a split or a one-sided draw needs
    // an injected fault on the activity to explain it.
    void checkDuelDraws(const Activity& activity) {
        if (!activity.drew[0] && !activity.drew[1]) {
            counts["duelsNeverDrawn"]++;
            return;
        }
        if (activity.drew[0] && activity.drew[1] && activity.drawn[0] == activity.drawn[1]) return;
        const std::string what =
        "duel action " + std::to_string(activity.action) + ": hunter drew " +
        (activity.drew[0] ? activity.drawn[0] : "nothing") + ", bounty drew " +
        (activity.drew[1] ? activity.drawn[1] : "nothing");
        if (activity.causes.empty()) {
            fail(what + ", with nothing disturbing it");
            return;
        }
        counts["splitDraw_" + *activity.causes.begin()]++;
        note(what);
    }

    Activity* activityOf(int pdn) {
        const int action = pdns[static_cast<size_t>(pdn)].activity;
        for (Activity& activity : activities) {
            if (action >= 0 && activity.action == static_cast<size_t>(action)) return &activity;
        }
        return nullptr;
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
            if (!slot.supporterPressed) {
                slot.supporterPressed = true;
                slot.supporterPressAt = now;
            }
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
            slot.countdownAt = now;
            slot.mashDone = 0;
            if (!slot.planned) {
                const uint64_t h = mix(static_cast<uint64_t>(index), ++slot.matchOrdinal);
                slot.reaction = h % 20 == 0 ? -1 : static_cast<int64_t>(150 + h % 450);
                slot.mash = 0;
            }
            slot.planned = false;
        } else if (state == MATCH && matchManager->getCurrentMatch().has_value()) {
            slot.matchId = canonicalId(matchManager->getCurrentMatch()->getMatchId());
            // The draw starts on this tick, by this PDN's own clock.
            slot.matchStart = slot.clock->milliseconds();
            if (matchManager->getMatchLocalStartTime() != slot.matchStart) {
                fail("pdn " + std::to_string(index) + " entered MATCH at " +
                     std::to_string(slot.matchStart) + " ms but its match starts at " +
                     std::to_string(matchManager->getMatchLocalStartTime()) + " ms");
            }
            TruthMatch& match = truth[slot.matchId];
            if (match.hunter.slot < 0 && match.bounty.slot < 0) match.drawAt = now;
            match.shootout = slot.matchId.rfind(kShootoutMatchIdPrefix, 0) == 0;
            if (match.shootout) {
                UUIDBytes stored;
                IdGenerator::uuidStringToBytes(slot.matchId, stored.bytes);
                shootoutIds.insert(formatUuid(stored));
            }
            (slot.identity.hunter ? match.hunter : match.bounty).slot = index;
            if (now < medium.burstUntil) match.causes.insert("burst");
            Activity* activity = activityOf(index);
            if (activity != nullptr && actions[activity->action].type == ActionType::DUEL) {
                const Action& action = actions[activity->action];
                const int side = index == action.a[0] ? 0 : index == action.b[0] ? 1 : -1;
                if (side >= 0 && !activity->drew[side]) {
                    activity->drew[side] = true;
                    activity->drawn[side] = slot.matchId;
                }
            }
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

    // Whether supporter @p s's confirm counts toward challenger @p c's boost on this tick,
    // from what the harness made happen and what reached each radio (chain-roster spec:
    // a confirm pressed after the challenger's countdown, from a member of its posse):
    // 2 surely, 1 only if a frame on a boundary tick was handled first, 0 not, with @p cause.
    int confirmCounts(int c, int s, bool direct, const Activity& activity,
                      std::string& cause) const {
        const PdnSlot& challenger = pdns[static_cast<size_t>(c)];
        const PdnSlot& supporter = pdns[static_cast<size_t>(s)];
        if (!supporter.confirm) {
            cause = "notTold";
            return 0;
        }
        if (!supporter.supporterPressed) {
            cause = "neverReady";
            return 0;
        }
        const uint64_t until = now;
        const auto within = [until](uint64_t at, uint64_t from) {
            if (at < from || at > until) return 0;
            return at == from || at == until ? 1 : 2;
        };
        // The cable makes the first supporter a member; one further down joins by radio.
        int member = direct ? 2 : 0;
        for (const Heard& heard : challenger.heard) {
            if (heard.type == PktType::kChainJoin && heard.from == s)
                member = std::max(member, within(heard.at, activity.startedAt));
        }
        if (member == 0) {
            cause = "joinNotHeard";
            return 0;
        }
        bool armed = false;
        for (const Heard& heard : supporter.heard) {
            armed = armed || (heard.type == PktType::kChainGameEvent && heard.from == c &&
                              heard.at >= challenger.countdownAt);
        }
        int confirmed = 0;
        const uint64_t from = std::max(challenger.countdownAt, supporter.supporterPressAt);
        for (const Heard& heard : challenger.heard) {
            if (heard.type == PktType::kChainConfirm && heard.from == s)
                confirmed = std::max(confirmed, within(heard.at, from));
        }
        if (confirmed == 0) {
            cause = armed ? "confirmNotHeard" : "countdownNotHeard";
            return 0;
        }
        return std::min(member, confirmed);
    }

    void recordPress(int index, uint32_t raw) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        TruthMatch& match = truth[slot.matchId];
        Side& side = slot.identity.hunter ? match.hunter : match.bounty;
        side.pressed = true;
        side.raw = raw;
        const uint32_t firmware = static_cast<uint32_t>(
        std::min(slot.device->context().posseManager->getBoostMs(), PosseManager::getMaxBoostMs()));
        side.boost = firmware;
        const Activity* activity = activityOf(index);
        if (activity == nullptr || match.shootout) return;
        const Action& action = actions[activity->action];
        if (action.type != ActionType::DUEL) return;
        const std::vector<int>* team = action.a[0] == index   ? &action.a
                                       : action.b[0] == index ? &action.b
                                                              : nullptr;
        if (team == nullptr) return;
        // The boost the harness itself expects: 15 ms per supporter it cabled into this
        // challenger's chain and made press, whose confirm reached it before this press.
        unsigned long sure = 0;
        unsigned long most = 0;
        std::string supporters;
        for (size_t i = 1; i < team->size(); i++) {
            const int supporter = (*team)[i];
            std::string cause;
            const int counted = confirmCounts(index, supporter, i == 1, *activity, cause);
            sure += counted == 2 ? PosseManager::BOOST_PER_SUPPORTER_MS : 0;
            most += counted >= 1 ? PosseManager::BOOST_PER_SUPPORTER_MS : 0;
            if (counted == 0 && cause != "notTold") counts["confirmMissed_" + cause]++;
            supporters += " " + std::to_string(supporter) + ":" +
                          (counted == 2   ? "counted"
                           : counted == 1 ? "boundary"
                                          : cause);
        }
        const uint32_t low = static_cast<uint32_t>(std::min(sure, PosseManager::getMaxBoostMs()));
        const uint32_t high = static_cast<uint32_t>(std::min(most, PosseManager::getMaxBoostMs()));
        if (firmware >= low && firmware <= high) {
            counts["boostAsExpected"]++;
            return;
        }
        const std::string what =
        "pdn " + std::to_string(index) + " drew " + slot.matchId + " with a " +
        std::to_string(firmware) + " ms boost, the harness counts " + std::to_string(low) +
        (high != low ? ".." + std::to_string(high) : "") + " ms (supporters" + supporters + ")";
        if (!activity->causes.empty()) {
            counts["boostExplained_" + *activity->causes.begin()]++;
            note(what);
            return;
        }
        side.boost = low;
        fail(what);
    }

    // The device mounted MatchResult, so finalizeMatch ran this tick.
    void onFinalize(int index) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        TruthMatch& match = truth[slot.matchId];
        Side& side = slot.identity.hunter ? match.hunter : match.bounty;
        side.finalized = true;
        const std::string id = slot.matchId;
        slot.matchId.clear();
        if (match.shootout || slot.committedThisTick == id) return;
        const uint32_t seq = slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0);
        const uint32_t collected = slot.nvs.getUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", 0);
        // Allowed: the ring refuses a match while 32 wait for an FDN (match-manager.cpp:348-355,
        // spec "full ring refuses"); a power cut before "seq" is written loses the match by design
        // (seq moves last, match-manager.cpp:366-371).
        if (seq - std::min(collected, seq) >= MatchManager::MATCH_RING_CAPACITY) {
            side.excuse = "ringFull";
            slot.ringRefused = true;
        } else if (slot.nvs.isFrozen()) {
            side.excuse = "cutBeforeCommit";
        } else {
            fail("pdn " + std::to_string(index) + " finalized " + id + " but stored nothing");
            return;
        }
        counts[side.excuse == "ringFull" ? "allowedRingFull" : "allowedCutBeforeCommit"]++;
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
        commit.sinceSyncMs = slot.clock->millisecondsSinceSync();
        commit.trueUnixMs = epochMs + static_cast<int64_t>(now);
        commits.push_back(commit);
        slot.committedThisTick = commit.id;
        if (slot.ringRefused) counts["ringStoredAgainAfterFull"]++;
        slot.ringRefused = false;
        note("commit pdn=" + std::to_string(index) + " seq=" + std::to_string(seq) +
             " id=" + commit.id);
    }

    // ---- drain and checks ----------------------------------------------------

    void startDrain() {
        note("drain");
        draining = true;
        medium.burstUntil = medium.dropAcksUntil = 0;
        if (fake != nullptr) fake->eventActive = true;
        noEventUntil = 0;
        for (FdnSlot& slot : fdns) {
            slot.mutedUntil = slot.offlineUntil = slot.httpErrorUntil = 0;
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
        if (commit.slot != match.hunter.slot && commit.slot != match.bounty.slot) {
            fail(where + ": stored by a PDN that is not a principal of that duel");
        }
        const MatchRecord& record = commit.record;
        const Side* sides[2] = {&match.hunter, &match.bounty};
        const UUIDBytes* players[2] = {&record.hunterPlayerId, &record.bountyPlayerId};
        const UUIDBytes* teams[2] = {&record.hunterTeamId, &record.bountyTeamId};
        const uint32_t times[2] = {record.hunterDrawTimeMs, record.bountyDrawTimeMs};
        const bool clean = match.causes.empty() && match.hunter.finalized && match.bounty.finalized;
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
        // A disturbed copy that carries both pressed times got both results, so its
        // winner follows from them as a clean one's does.
        const bool bothTimes = match.hunter.pressed && match.bounty.pressed &&
                               times[0] == match.hunter.raw && times[1] == match.bounty.raw;
        if (clean || bothTimes) {
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
        }
        counts[clean       ? "cleanCopiesChecked"
               : bothTimes ? "disturbedCopiesWinnerChecked"
                           : "disturbedCopies"]++;
        // An unset clock stamps uptime (#518). The RTC survives a soft reboot and is lost
        // with power, so a PDN an FDN synced in this power-on must stamp UTC.
        const bool uptimeStamp = record.occurredAtUnixMs < epochMs - UPTIME_STAMP_BEFORE_MS;
        if (uptimeStamp) {
            if (commit.synced) {
                fail(where + ": uptime stamp " + std::to_string(record.occurredAtUnixMs) +
                     " from a PDN whose clock an FDN set in this power-on");
            } else {
                counts["knownUnsyncedStamp518"]++;
            }
            return;
        }
        if (!commit.synced) {
            fail(where + ": UTC stamp " + std::to_string(record.occurredAtUnixMs) +
                 " from a PDN no FDN synced in this power-on");
            return;
        }
        counts["syncedStampsChecked"]++;
        // The context carries the FDN's time at its first send and is read on a later
        // tick, so a stamp trails the truth by up to the retransmit span plus the air,
        // and leads it only by the PDN's drift since the sync.
        const int64_t drift =
        static_cast<int64_t>(commit.sinceSyncMs) * MAX_DRIFT_PPM / 1000000 + 1;
        const int64_t trail =
        static_cast<int64_t>(Resender::RETRANSMIT_SPAN_MS + config.medium.maxDelayMs +
                             config.medium.maxReorderDelayMs + TICK_MS) +
        drift;
        const int64_t offset = record.occurredAtUnixMs - commit.trueUnixMs;
        if (offset < -trail || offset > drift) {
            fail(where + ": occurred-at off by " + std::to_string(offset) + " ms (allowed -" +
                 std::to_string(trail) + "..+" + std::to_string(drift) + ")");
        }
    }

    // Every played duel is accounted for: each principal that drew stores its record,
    // unless a classified cause explains why not (quickdraw-match spec: abandonment on a
    // disconnect or reboot, the 4 s timeout when neither pressed, the full ring, a power
    // cut before "seq").
    void checkDuelsAccounted() {
        std::map<std::pair<int, std::string>, int> stored;
        for (const Commit& commit : commits) {
            if (++stored[{commit.slot, commit.id}] == 2) {
                fail("pdn " + std::to_string(commit.slot) + " stored " + commit.id + " twice");
            }
        }
        for (const std::pair<const std::string, TruthMatch>& entry : truth) {
            const TruthMatch& match = entry.second;
            if (match.shootout || match.hunter.slot < 0 || match.bounty.slot < 0) continue;
            counts["duelsBothDrew"]++;
            const bool neitherPressed = !match.hunter.pressed && !match.bounty.pressed;
            for (const Side* side : {&match.hunter, &match.bounty}) {
                if (stored.count({side->slot, entry.first}) != 0) continue;
                // A finalize that stored nothing unexcused already failed in onFinalize.
                if (side->finalized && side->excuse.empty()) continue;
                std::string cause = side->excuse;
                if (cause.empty() && neitherPressed) cause = "neitherPressed";
                if (cause.empty() && !match.causes.empty()) cause = *match.causes.begin();
                if (cause.empty()) {
                    fail("pdn " + std::to_string(side->slot) + " drew " + entry.first +
                         " and never finalized it, with nothing disturbing it");
                    continue;
                }
                counts["noRecord_" + cause]++;
            }
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
            fail("could not read the server's rows for this run's event");
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
            if (extra <= 0) continue;
            counts["extraReports"] += static_cast<uint64_t>(extra);
            // Each extra report needs a counted resend: a copy an FDN stored again, an
            // upload the server took that the FDN never heard succeed, a lost FDN head write.
            const uint64_t explained =
            resendAllowance[entry.first] + unconfirmedUploads[entry.first];
            if (static_cast<uint64_t>(extra) > explained) {
                counts["extraReportsUnexplained"] += static_cast<uint64_t>(extra) - explained;
                fail("server got " + std::to_string(extra) + " extra reports of " + entry.first +
                     ", " + std::to_string(explained) + " explained by counted resends");
            }
        }
        for (const std::string& id : airIds) {
            if (copies.count(id) == 0 || shootoutIds.count(id) != 0) {
                fail("relayed " + id + describe(id) + " was never stored as a duel");
            }
        }
        // With scoring on from before the first stamp, a decided match stamped in UTC
        // scores and one stamped in uptime (#518) falls outside the period.
        for (const ServerRow& row : rows) {
            if (row.scoringStatus.empty()) continue;
            counts["scoring_" + row.scoringStatus]++;
            const bool uptimeStamp = row.occurredAtMs < epochMs - UPTIME_STAMP_BEFORE_MS;
            const char* expected = row.winner.empty() ? "tie"
                                   : uptimeStamp      ? "scoring_disabled"
                                                      : "scoring";
            if (row.scoringStatus != expected) {
                fail("server row " + row.id + " is " + row.scoringStatus + ", expected " +
                     expected);
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
            if (seq > MatchManager::MATCH_RING_CAPACITY) counts["pdnRingsWrapped"]++;
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
            if (tail > FDN_MATCH_RECORD_CAPACITY) counts["fdnStoresWrapped"]++;
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
            checkDuelsAccounted();
            checkServer();
            checkInvariants(drained);
            if (!drained) fail("drain did not quiesce within the cap");
            if (config.schedule.fdnBlackoutMs > 0 && counts["allowedRingFull"] == 0)
                fail("ring-fill mode: no PDN ring ever filled");
            if (config.schedule.fdnBlackoutMs > 0 && counts["ringStoredAgainAfterFull"] == 0)
                fail("ring-fill mode: no PDN stored a match again after its full ring refused one");
        }
        uint64_t cutsArmed = 0;
        uint64_t cutsFired = 0;
        for (const PdnSlot& slot : pdns) {
            cutsArmed += slot.nvs.cutsArmed;
            cutsFired += slot.nvs.cutsFired;
        }
        for (const FdnSlot& slot : fdns) {
            cutsArmed += slot.nvs.cutsArmed;
            cutsFired += slot.nvs.cutsFired;
        }
        counts["cutsArmed"] = cutsArmed;
        counts["cutsFired"] = cutsFired;
        counts["cutsArmedNeverFired"] = cutsArmed - cutsFired;
        counts["duelRebootsNeverFired"] = counts["duelRebootsPlanned"] - counts["duelRebootsFired"];
        // Printed even when zero: the #518 class and the wrap evidence are read per seed.
        for (const char* key :
             {"knownUnsyncedStamp518", "syncedStampsChecked", "pdnRingsWrapped", "fdnStoresWrapped",
              "allowedRingFull", "allowedCutBeforeCommit", "storedMatches", "serverRows",
              "allowedFdnRefusals", "boostAsExpected", "extraReports", "extraReportsUnexplained",
              "actionsSkipped", "actionsNotApplicable"}) {
            counts[key] += 0;
        }
        const uint64_t stamps = counts["knownUnsyncedStamp518"] + counts["syncedStampsChecked"];
        counts["unsyncedStampPercent"] =
        stamps == 0 ? 0 : counts["knownUnsyncedStamp518"] * 100 / stamps;
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
        char head[224];
        snprintf(
        head, sizeof(head),
        "seed=%llu pdns=%d fdns=%d minutes=%llu actions=%zu %s fails=%zu hash=%016llx "
        "nonce=%llu",
        static_cast<unsigned long long>(config.schedule.seed), config.schedule.pdns,
        config.schedule.fdns, static_cast<unsigned long long>(config.schedule.horizonMs / 60000),
        actions.size(), result.passed ? "PASS" : "FAIL", failures.size(),
        static_cast<unsigned long long>(result.logHash), static_cast<unsigned long long>(nonce));
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
    // The wall clock at virtual time zero: fixed on the fake, the real clock on a real server.
    int64_t epochMs = EVENT_EPOCH_MS;
    // Mixed into the entropy seed (setUp); 0 until set up.
    uint64_t nonce = 0;
    std::vector<PdnSlot> pdns;
    std::vector<FdnSlot> fdns;
    std::vector<Activity> activities;
    size_t nextAction = 0;
    std::vector<size_t> deferred;
    std::map<std::string, TruthMatch> truth;
    std::vector<Commit> commits;
    // Extra server reports each match id may show: copies stored again by an FDN (here
    // after a retire, or at a second FDN) and records an FDN re-uploads after losing its
    // head write; and uploads the server took that the FDN never heard succeed.
    std::map<std::string, uint64_t> resendAllowance;
    std::map<std::string, uint64_t> unconfirmedUploads;
    std::set<std::string> storedRecords;
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
