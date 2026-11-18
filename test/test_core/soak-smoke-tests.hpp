#pragma once

#include <gtest/gtest.h>

#include "soak/soak-world.hpp"

// Short soak runs on the in-process fake server: a few PDNs and FDNs, a minute
// and a half of scheduled play under loss, faults and HTTP errors, then a drain
// and the full end-to-end check. A seed that fails here replays exactly.
inline soak::SoakConfig soakSmokeConfig(uint64_t seed) {
    soak::SoakConfig config;
    config.schedule.seed = seed;
    config.schedule.pdns = 8;
    config.schedule.fdns = 2;
    config.schedule.horizonMs = 90000;
    config.schedule.rebootsPerHour = 6;
    config.schedule.cutsPerHour = 6;
    config.schedule.fdnRebootsPerHour = 20;
    config.schedule.httpErrorsPerHour = 40;
    config.schedule.ackLossPerHour = 120;
    // High enough that a short run resends records an FDN already holds (lost
    // link-layer acks) and sees some upload batches answered 503 or lost; the
    // scheduled windows add whole stretches of them.
    config.medium.ackLoss = 0.15;
    config.http.serverError = 0.15;
    config.http.transportFail = 0.05;
    config.http.lostResponse = 0.05;
    // One upload try per FDN per 300 s: room for several 503s in a row.
    config.drainCapMs = 40 * 60000;
    return config;
}

inline void expectSoakPasses(const soak::RunResult& result) {
    std::string failures;
    for (const std::string& failure : result.failures)
        failures += failure + "\n";
    EXPECT_TRUE(result.passed) << result.summary << "\n" << failures;
}

// Test: a seeded event passes every end-to-end check
inline void soakSmokeSeedPasses(uint64_t seed) {
    soak::RunResult result = soak::runSeed(soakSmokeConfig(seed));
    expectSoakPasses(result);
    EXPECT_GT(result.counts["storedMatches"], 0u) << result.summary;
}

// Test: the same seed twice plays the same event, tick for tick
inline void soakSameSeedReplaysIdentically() {
    soak::SoakConfig config = soakSmokeConfig(5);
    config.schedule.pdns = 6;
    config.schedule.horizonMs = 30000;
    config.http = soak::HttpFaults();
    const soak::RunResult first = soak::runSeed(config);
    const soak::RunResult second = soak::runSeed(config);
    EXPECT_EQ(first.logHash, second.logHash);
    EXPECT_EQ(first.log, second.log);
}

// Test: a written action list parses back to the same list
inline void soakActionListRoundTrips() {
    soak::ScheduleConfig config;
    config.horizonMs = 60000;
    for (const soak::Action& action : soak::Scheduler::generate(config)) {
        soak::Action parsed;
        ASSERT_TRUE(soak::parseAction(soak::formatAction(action), parsed));
        EXPECT_EQ(soak::formatAction(parsed), soak::formatAction(action));
    }
}

// A scripted event on @p pdns PDNs and @p fdns FDNs with no random HTTP faults.
inline soak::RunResult playSoakScript(int pdns, int fdns, const std::vector<std::string>& lines) {
    soak::SoakConfig config = soakSmokeConfig(7);
    config.schedule.pdns = pdns;
    config.schedule.fdns = fdns;
    config.http = soak::HttpFaults();
    std::vector<soak::Action> actions;
    for (const std::string& line : lines) {
        soak::Action action;
        EXPECT_TRUE(soak::parseAction(line, action)) << line;
        actions.push_back(action);
    }
    // Room for the FDN's boot-time upload cycle before the drain clears the faults.
    config.schedule.horizonMs = actions.back().at + 10000;
    soak::World world(config, actions);
    soak::RunResult result = world.run();
    expectSoakPasses(result);
    return result;
}

// Test: a shootout ring next to duels and a posse stores only the duels, and they all arrive
inline void soakRingAndPosseStoreOnlyDuels() {
    soak::RunResult result = playSoakScript(11, 2,
                                            {
                                            "at=1000 ring a=0,2,4 b=- cm=15",
                                            "at=1000 duel a=6,8 b=1,3 cm=3 rb=260 rh=310",
                                            "at=1000 visit a=7 b=0 dur=4000",
                                            "at=12000 duel a=10 b=7 rb=-1 rh=280",
                                            });
    EXPECT_GT(result.counts["shootoutMatches"], 0u) << result.summary;
    EXPECT_GE(result.counts["storedMatches"], 2u) << result.summary;
}

// Test: matches still arrive when one has a single stored copy, every record ack
// is lost for a while (the PDNs resend what the FDN holds), and the FDN reboots
// into a window where every upload answers 503
inline void soakSingleCopiesOutliveLostAcksAndServerErrors() {
    soak::RunResult result = playSoakScript(4, 1,
                                            {
                                            "at=1000 visit a=1 b=0 dur=2500",
                                            "at=1000 duel a=0 b=1 cut=0 k=1 rb=300 rh=250",
                                            "at=1000 duel a=2 b=3 rb=-1 rh=200",
                                            "at=3000 ackloss a=- b=- dur=30000",
                                            "at=40000 httperr a=- b=0 dur=60000 mode=1",
                                            "at=41000 fdnreboot a=- b=0 down=1000",
                                            });
    EXPECT_GE(result.counts["allowedCutBeforeCommit"], 1u) << result.summary;
    EXPECT_GE(result.counts["storedMatches"], 2u) << result.summary;
    EXPECT_GE(result.counts["syncedStampsChecked"], 1u) << result.summary;
}
