#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace soak {

// What a run does, as intents with a virtual start time. The world decides the
// ticks (it waits for the PDNs to be idle and presses relative to the draw), so
// dropping actions from a list still leaves a runnable list: that is what lets a
// failing seed shrink.
enum class ActionType { DUEL, RING, VISIT, REBOOT, CUT, FDN_OFF, FDN_REBOOT, BURST };

struct Action {
    uint64_t at = 0;
    ActionType type = ActionType::DUEL;
    // DUEL: challenger hunter then its supporters, nearest first. RING: members.
    // VISIT, REBOOT, CUT: the PDN.
    std::vector<int> a;
    // DUEL: bounty then its supporters. VISIT, FDN_OFF, FDN_REBOOT: the FDN.
    std::vector<int> b;
    std::map<std::string, int64_t> params;

    /** A parameter, or @p fallback when absent. */
    int64_t get(const std::string& key, int64_t fallback = 0) const {
        std::map<std::string, int64_t>::const_iterator it = params.find(key);
        return it == params.end() ? fallback : it->second;
    }
};

inline const char* const ACTION_NAMES[] = {"duel", "ring",   "visit",     "reboot",
                                           "cut",  "fdnoff", "fdnreboot", "burst"};

inline std::string formatList(const std::vector<int>& list) {
    std::string out;
    for (size_t i = 0; i < list.size(); i++)
        out += (i ? "," : "") + std::to_string(list[i]);
    return out.empty() ? "-" : out;
}

/** One action per line: "at=<ms> <type> a=<list> b=<list> key=value...". */
inline std::string formatAction(const Action& action) {
    std::string out = "at=" + std::to_string(action.at) + " " +
                      ACTION_NAMES[static_cast<int>(action.type)] + " a=" + formatList(action.a) +
                      " b=" + formatList(action.b);
    for (const std::pair<const std::string, int64_t>& param : action.params) {
        out += " " + param.first + "=" + std::to_string(param.second);
    }
    return out;
}

/** Parses formatAction output; false on a malformed line. */
inline bool parseAction(const std::string& line, Action& action) {
    std::istringstream words(line);
    std::string word;
    bool typed = false;
    while (words >> word) {
        const size_t eq = word.find('=');
        if (eq == std::string::npos) {
            for (int i = 0; i < 8; i++) {
                if (word == ACTION_NAMES[i]) {
                    action.type = static_cast<ActionType>(i);
                    typed = true;
                }
            }
            continue;
        }
        const std::string key = word.substr(0, eq);
        const std::string value = word.substr(eq + 1);
        if (key == "a" || key == "b") {
            std::vector<int>& list = key == "a" ? action.a : action.b;
            std::istringstream items(value);
            std::string item;
            while (std::getline(items, item, ',')) {
                if (item != "-") list.push_back(std::stoi(item));
            }
        } else if (key == "at") {
            action.at = std::stoull(value);
        } else {
            action.params[key] = std::stoll(value);
        }
    }
    return typed;
}

struct ScheduleConfig {
    uint64_t seed = 1;
    int pdns = 8;
    int fdns = 2;
    uint64_t horizonMs = 120000;
    // Per device per virtual hour.
    double rebootsPerHour = 0.6;
    double cutsPerHour = 0.6;
    double fdnOffPerHour = 6;
    double fdnRebootsPerHour = 2;
    double burstsPerHour = 20;
};

// Seeded generator of an action list. It keeps a rough busy-until per device so
// most actions find their devices free; the world skips the rest.
class Scheduler {
public:
    static constexpr uint64_t STEP_MS = 250;
    // A duel ends on Win or Lose (8 s), then Sleep (60 s) and the awaken sequence.
    static constexpr uint64_t DUEL_BUSY_MS = 76000;
    static constexpr uint64_t RING_BUSY_MS = 210000;

    /** The action list for @p config, a pure function of it. */
    static std::vector<Action> generate(const ScheduleConfig& config) {
        Scheduler scheduler(config);
        return scheduler.run();
    }

private:
    explicit Scheduler(const ScheduleConfig& config)
        : config(config)
        , rng(config.seed * 0x9E3779B97F4A7C15ULL + 1)
        , pdnBusy(static_cast<size_t>(config.pdns), 0)
        , fdnBusy(static_cast<size_t>(config.fdns), 0) {}

    bool chance(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }
    int64_t uniform(int64_t low, int64_t high) {
        return std::uniform_int_distribution<int64_t>(low, high)(rng);
    }
    double perStep(double perHour, int devices) const {
        return perHour * devices * static_cast<double>(STEP_MS) / 3600000.0;
    }
    int64_t reaction() { return chance(0.06) ? -1 : uniform(150, 600); }

    // Free PDNs of one role (hunters are the even slots), shuffled.
    std::vector<int> freePdns(uint64_t t, int role) {
        std::vector<int> out;
        for (int i = 0; i < config.pdns; i++) {
            if (pdnBusy[static_cast<size_t>(i)] <= t && (role < 0 || i % 2 == role)) {
                out.push_back(i);
            }
        }
        std::shuffle(out.begin(), out.end(), rng);
        return out;
    }

    void occupy(const std::vector<int>& pdns, uint64_t until) {
        for (int pdn : pdns)
            pdnBusy[static_cast<size_t>(pdn)] = until;
    }

    void addDuel(uint64_t t, bool posse) {
        std::vector<int> hunters = freePdns(t, 0);
        std::vector<int> bounties = freePdns(t, 1);
        if (hunters.empty() || bounties.empty()) return;
        Action duel;
        duel.at = t;
        duel.type = ActionType::DUEL;
        const size_t hunterSide = posse ? std::min<size_t>(hunters.size(), uniform(1, 3)) : 1;
        const size_t bountySide = posse ? std::min<size_t>(bounties.size(), uniform(1, 3)) : 1;
        duel.a.assign(hunters.begin(), hunters.begin() + static_cast<long>(hunterSide));
        duel.b.assign(bounties.begin(), bounties.begin() + static_cast<long>(bountySide));
        duel.params["rh"] = reaction();
        duel.params["rb"] = reaction();
        if (chance(0.1)) duel.params["mh"] = uniform(1, 2);
        if (chance(0.1)) duel.params["mb"] = uniform(1, 2);
        if (posse) duel.params["cm"] = uniform(0, 15) | (chance(0.8) ? 15 : 0);
        if (chance(0.15)) duel.params["bounce"] = uniform(1, 3);
        const double fault = std::uniform_real_distribution<double>(0, 1)(rng);
        if (fault < 0.08) {
            duel.params["reboot"] = uniform(0, 1);
            duel.params["rebootdelay"] = uniform(0, 900);
        } else if (fault < 0.16) {
            duel.params["cut"] = uniform(0, 1);
            duel.params["k"] = uniform(1, 3);
            duel.params["torn"] = uniform(0, 1);
        }
        occupy(duel.a, t + DUEL_BUSY_MS);
        occupy(duel.b, t + DUEL_BUSY_MS);
        actions.push_back(duel);
    }

    void addRing(uint64_t t) {
        std::vector<int> members = freePdns(t, static_cast<int>(uniform(0, 1)));
        if (members.size() < 3) return;
        Action ring;
        ring.at = t;
        ring.type = ActionType::RING;
        const size_t size = std::min<size_t>(members.size(), static_cast<size_t>(uniform(3, 4)));
        ring.a.assign(members.begin(), members.begin() + static_cast<long>(size));
        ring.params["cm"] = chance(0.85) ? 15 : uniform(0, 15);
        occupy(ring.a, t + RING_BUSY_MS);
        actions.push_back(ring);
    }

    void addVisit(uint64_t t) {
        std::vector<int> free = freePdns(t, -1);
        const int fdn = static_cast<int>(uniform(0, config.fdns - 1));
        if (free.empty() || fdnBusy[static_cast<size_t>(fdn)] > t) return;
        Action visit;
        visit.at = t;
        visit.type = ActionType::VISIT;
        visit.a = {free[0]};
        visit.b = {fdn};
        visit.params["dur"] = uniform(2000, 12000);
        occupy(visit.a, t + static_cast<uint64_t>(visit.params["dur"]) + 6000);
        fdnBusy[static_cast<size_t>(fdn)] = t + static_cast<uint64_t>(visit.params["dur"]) + 2000;
        actions.push_back(visit);
    }

    void addFaults(uint64_t t) {
        Action fault;
        fault.at = t;
        if (chance(perStep(config.burstsPerHour, 1))) {
            fault.type = ActionType::BURST;
            fault.params = {{"dur", uniform(500, 6000)}, {"loss", uniform(30, 95)}};
            actions.push_back(fault);
        }
        if (chance(perStep(config.rebootsPerHour, config.pdns))) {
            fault.type = ActionType::REBOOT;
            fault.a = {static_cast<int>(uniform(0, config.pdns - 1))};
            fault.params = {{"down", uniform(800, 3000)}};
            actions.push_back(fault);
        }
        if (chance(perStep(config.cutsPerHour, config.pdns))) {
            fault.type = ActionType::CUT;
            fault.a = {static_cast<int>(uniform(0, config.pdns - 1))};
            fault.params = {
            {"k", uniform(1, 4)}, {"torn", uniform(0, 1)}, {"down", uniform(800, 5000)}};
            actions.push_back(fault);
        }
        fault.a.clear();
        if (chance(perStep(config.fdnOffPerHour, config.fdns))) {
            fault.type = ActionType::FDN_OFF;
            fault.b = {static_cast<int>(uniform(0, config.fdns - 1))};
            fault.params = {{"dur", uniform(30000, 400000)}, {"mode", uniform(1, 3)}};
            actions.push_back(fault);
        }
        if (chance(perStep(config.fdnRebootsPerHour, config.fdns))) {
            fault.type = ActionType::FDN_REBOOT;
            fault.b = {static_cast<int>(uniform(0, config.fdns - 1))};
            fault.params = {{"down", uniform(1000, 4000)},
                            {"k", chance(0.4) ? uniform(1, 3) : 0},
                            {"torn", uniform(0, 1)}};
            actions.push_back(fault);
        }
    }

    std::vector<Action> run() {
        const int attempts = std::max(1, config.pdns / 12);
        for (uint64_t t = 3000; t < config.horizonMs; t += STEP_MS) {
            addFaults(t);
            for (int i = 0; i < attempts; i++) {
                if (!chance(0.35)) continue;
                const int64_t kind = uniform(0, 99);
                if (kind < 58) {
                    addDuel(t, false);
                } else if (kind < 76) {
                    addDuel(t, true);
                } else if (kind < 82) {
                    addRing(t);
                } else {
                    addVisit(t);
                }
            }
        }
        return actions;
    }

    ScheduleConfig config;
    std::mt19937_64 rng;
    std::vector<uint64_t> pdnBusy;
    std::vector<uint64_t> fdnBusy;
    std::vector<Action> actions;
};

}  // namespace soak
