#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "device/driver-names.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/pdn.hpp"
#include "soak/sim-medium.hpp"
#include "soak/soak-world.hpp"

// Enabled radios at MACs 02:50:00:00:00:<i> on a medium seeded with 7; every
// kMatchCommand frame a radio handles is written down as "<to>:<from>:<byte>".
struct SoakAir {
    /** @p count radios. */
    explicit SoakAir(int count) {
        for (int i = 0; i < count; i++) {
            const uint8_t mac[6] = {0x02, 0x50, 0, 0, 0, static_cast<uint8_t>(i)};
            soak::SimRadio* radio = new soak::SimRadio("radio", medium, mac);
            radio->initialize();
            radio->enable();
            radio->setPacketHandler(
            PktType::kMatchCommand,
            [this, i](const uint8_t* src, const uint8_t* data, size_t, void*) {
                heard.push_back(std::to_string(i) + ":" + std::to_string(src[5]) + ":" +
                                std::to_string(data[0]));
            },
            nullptr);
            radio->setSendStatusHandler(
            PktType::kMatchCommand,
            [this](const uint8_t*, const uint8_t*, size_t, bool ok, void*) {
                ++(ok ? sent : failed);
            },
            nullptr);
            radios.push_back(radio);
        }
    }

    /** Frees the radios. */
    ~SoakAir() {
        for (soak::SimRadio* radio : radios)
            delete radio;
    }

    /** Radio @p from sends the byte @p value to radio @p to, or to everyone when @p to < 0. */
    int send(int from, int to, uint8_t value, PktType type = PktType::kMatchCommand) {
        const uint8_t* dst = to < 0 ? soak::SimRadio::BROADCAST : radios[to]->getMacAddress();
        return radios[from]->sendData(dst, type, &value, 1);
    }

    /** Advances virtual time by @p ms in 5 ms ticks, delivering and handling as it goes. */
    void run(uint64_t ms) {
        for (const uint64_t end = now + ms; now < end; now += 5) {
            medium.deliverDue();
            for (soak::SimRadio* radio : radios)
                radio->exec();
        }
    }

    uint64_t now = 0;
    soak::VirtualMedium medium{7, now};
    std::vector<soak::SimRadio*> radios;
    std::vector<std::string> heard;
    int sent = 0;
    int failed = 0;
};

// Test: millis restarts at each boot, the wall clock survives a soft reboot and
// is lost with power, and drift skews both
inline void soakClockKeepsWallTimeAcrossSoftReboot() {
    uint64_t now = 10000;
    soak::VirtualClock clock(now, 100);
    clock.boot(true);
    now = 20000;
    EXPECT_EQ(clock.milliseconds(), 10351u);
    EXPECT_EQ(clock.unixMilliseconds(), 10351);
    clock.setUnixMilliseconds(1795000000000LL);
    now = 30000;
    clock.boot(false);
    EXPECT_EQ(clock.milliseconds(), 350u);
    EXPECT_EQ(clock.unixMilliseconds(), 1795000010001LL);
    EXPECT_EQ(clock.millisecondsSinceSync(), 10000u);
    clock.boot(true);
    EXPECT_FALSE(clock.synced);
    EXPECT_EQ(clock.unixMilliseconds(), 350);
}

// Test: loss, duplication, reorder and lost link acks hit unicast frames, and the
// same seed replays the same frames in the same order
inline void soakMediumFaultsReplayFromSeed() {
    std::vector<std::string> runs[2];
    for (std::vector<std::string>& heard : runs) {
        SoakAir air(2);
        air.medium.config = {0.2, 0.2, 0.1, 0.3, 1, 6, 40};
        for (int i = 0; i < 200; i++) {
            air.send(0, 1, static_cast<uint8_t>(i));
            air.run(5);
        }
        air.run(100);
        const std::set<std::string> distinct(air.heard.begin(), air.heard.end());
        EXPECT_LT(distinct.size(), 190u);
        EXPECT_GT(air.heard.size(), distinct.size());
        EXPECT_FALSE(std::is_sorted(air.heard.begin(), air.heard.end(),
                                    [](const std::string& a, const std::string& b) {
                                        return std::stoi(a.substr(4)) < std::stoi(b.substr(4));
                                    }));
        EXPECT_EQ(air.sent + air.failed, 200);
        EXPECT_GT(air.failed, static_cast<int>(200 - distinct.size()));
        EXPECT_EQ(air.medium.framesLost, air.medium.framesSent - air.heard.size());
        heard = air.heard;
    }
    EXPECT_EQ(runs[0], runs[1]);
}

// Test: a broadcast reaches every other enabled radio and a unicast only its MAC;
// mute, a disabled radio, a loss burst and the record-ack loss window stop frames,
// and a delivered frame whose link ack is lost reports failure
inline void soakMediumRoutesByMac() {
    SoakAir air(3);
    air.medium.config = {0, 0, 0, 0, 1, 6, 40};
    air.send(0, -1, 1);
    air.send(0, 2, 2);
    air.run(20);
    std::sort(air.heard.begin(), air.heard.end());
    EXPECT_EQ(air.heard, (std::vector<std::string>{"1:0:1", "2:0:1", "2:0:2"}));
    EXPECT_EQ(air.sent, 1);
    air.heard.clear();
    air.medium.setMuted(air.radios[2]->getMacAddress(), true);
    air.radios[1]->disable();
    EXPECT_EQ(air.send(1, 0, 3), -1);
    air.send(0, 2, 4);
    air.send(2, 0, 5);
    air.send(0, -1, 6);
    air.run(20);
    EXPECT_TRUE(air.heard.empty());
    EXPECT_EQ(air.failed, 2);
    air.medium.setMuted(air.radios[2]->getMacAddress(), false);
    air.medium.burstLoss = 1;
    air.medium.burstUntil = air.now + 20;
    air.medium.dropAcksUntil = air.now + 40;
    air.send(0, 2, 7);
    air.run(20);
    air.send(0, 2, 8, PktType::kRecordAck);
    air.send(0, 2, 9);
    air.run(20);
    air.medium.config.ackLoss = 1;
    air.send(0, 2, 10);
    air.run(20);
    EXPECT_EQ(air.heard, (std::vector<std::string>{"2:0:9", "2:0:10"}));
    EXPECT_EQ(air.medium.framesLost, 6u);
    EXPECT_EQ(air.sent, 2);
    EXPECT_EQ(air.failed, 4);
}

// Test: a cable carries HELLO between two PDNs' real serial drivers until both
// jacks are CONNECTED (context over the medium), and unplugging drops both
inline void soakCablePlugConnectsAndUnplugDrops() {
    uint64_t now = 0;
    soak::VirtualClock clock(now, 0);
    PlatformClock* saved = SimpleTimer::getPlatformClock();
    SimpleTimer::setPlatformClock(&clock);
    soak::VirtualMedium medium(3, now);
    PDN* pdns[2] = {};
    NativeSerialDriver* jacks[2][2] = {};
    for (int i = 0; i < 2; i++) {
        const uint8_t mac[6] = {0x02, 0x50, 0, 0, 0, static_cast<uint8_t>(i)};
        const std::string suffix = "_" + std::to_string(i);
        jacks[i][0] = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        jacks[i][1] = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        DriverConfig config = {
        {LIGHT_DRIVER_NAME, new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix)},
        {SERIAL_OUT_DRIVER_NAME, jacks[i][0]},
        {SERIAL_IN_DRIVER_NAME, jacks[i][1]},
        {PEER_COMMS_DRIVER_NAME, new soak::SimRadio(PEER_COMMS_DRIVER_NAME + suffix, medium, mac)},
        };
        pdns[i] = PDN::createPDN(config);
        pdns[i]->begin();
    }
    soak::VirtualCables cables([&jacks](const soak::JackRef& jack) {
        return jacks[jack.device][jack.jack == SerialIdentifier::OUTPUT_JACK ? 0 : 1];
    });
    const soak::JackRef out = {0, SerialIdentifier::OUTPUT_JACK};
    const soak::JackRef in = {1, SerialIdentifier::INPUT_JACK};
    const auto run = [&](uint64_t ms) {
        for (const uint64_t end = now + ms; now < end;) {
            now += 5;
            medium.deliverDue();
            cables.transfer(
            {out, {0, SerialIdentifier::INPUT_JACK}, in, {1, SerialIdentifier::OUTPUT_JACK}});
            for (PDN* pdn : pdns) {
                if (now % RemoteDeviceCoordinator::HELLO_CADENCE_MS == 0)
                    pdn->getRemoteDeviceCoordinator()->emitHello();
                pdn->loop();
            }
        }
    };
    const auto connected = [&]() {
        return std::make_pair(pdns[0]->getRemoteDeviceCoordinator()->getJackStatus(out.jack),
                              pdns[1]->getRemoteDeviceCoordinator()->getJackStatus(in.jack));
    };
    ASSERT_TRUE(cables.plug(out, in));
    EXPECT_FALSE(cables.plug(in, {0, SerialIdentifier::INPUT_JACK}));
    run(1000);
    EXPECT_EQ(connected(), std::make_pair(JackStatus::CONNECTED, JackStatus::CONNECTED));
    cables.unplug(in);
    run(1000);
    EXPECT_EQ(connected(), std::make_pair(JackStatus::DISCONNECTED, JackStatus::DISCONNECTED));
    for (PDN* pdn : pdns)
        delete pdn;
    SimpleTimer::setPlatformClock(saved);
}

// Parses an action list written one action per line.
inline std::vector<soak::Action> soakScript(const std::vector<std::string>& lines) {
    std::vector<soak::Action> actions;
    for (const std::string& line : lines) {
        soak::Action action;
        EXPECT_TRUE(soak::parseAction(line, action)) << line;
        actions.push_back(action);
    }
    return actions;
}

// A World whose devices and server the test reads after the run.
struct SoakDuelWorld : soak::World {
    using World::fdns;
    using World::pdns;
    using World::server;
    using World::World;
};

// Test: two PDNs duel on a cable, an FDN collects both records over the medium
// and uploads them, and the fake server holds one accepted row from two reports
inline void soakFdnUploadsACabledDuel() {
    soak::SoakConfig config;
    config.pdns = 2;
    config.fdns = 1;
    // Past the uploader's first 300 s interval.
    config.schedule.horizonMs = 400000;
    SoakDuelWorld world(config, soakScript({"at=1000 duel a=0 b=1 rh=200 rb=300"}));
    world.run();
    // The FDN retired both records once the server answered their upload.
    EXPECT_EQ(world.fdns[0].nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_head", 0), 2u);
    std::vector<soak::ServerRow> rows;
    world.server->listMatches(rows);
    ASSERT_EQ(rows.size(), 1u);
    std::map<std::string, int> reports;
    world.server->countReports(reports);
    EXPECT_EQ(reports[rows[0].id], 2);
    EXPECT_EQ(rows[0].playerA, soak::formatUuid(world.pdns[0].identity.playerId));
    EXPECT_EQ(rows[0].playerB, soak::formatUuid(world.pdns[1].identity.playerId));
    EXPECT_EQ(rows[0].winner, rows[0].playerA);
    EXPECT_EQ(rows[0].drawA, 200u);
    EXPECT_EQ(soak::SimNvsDriver::unopenedNamespaceCalls, 0u);
}

// A short event: a few PDNs and FDNs, a minute and a half of scheduled play under
// loss, faults and HTTP errors.
inline soak::SoakConfig soakSmokeConfig(uint64_t seed) {
    soak::SoakConfig config;
    config.seed = seed;
    config.pdns = 8;
    config.fdns = 2;
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
    return config;
}

// The run passed every end-to-end check; the summary and findings say why not.
inline void expectSoakPasses(const soak::RunResult& result) {
    std::string failures;
    for (const std::string& failure : result.failures)
        failures += failure + "\n";
    EXPECT_TRUE(result.passed) << result.summary << "\n" << failures;
}

// A scripted event on @p pdns PDNs and @p fdns FDNs with no random HTTP faults; it
// must pass the end-to-end checks.
inline soak::RunResult playSoakScript(int pdns, int fdns, const std::vector<std::string>& lines) {
    soak::SoakConfig config = soakSmokeConfig(7);
    config.pdns = pdns;
    config.fdns = fdns;
    config.http = soak::HttpFaults();
    const std::vector<soak::Action> actions = soakScript(lines);
    // Room for the FDN's boot-time upload cycle before the drain clears the faults.
    config.schedule.horizonMs = actions.back().at + 10000;
    soak::World world(config, actions);
    soak::RunResult result = world.run();
    expectSoakPasses(result);
    return result;
}

// Test: a seeded event passes every end-to-end check
inline void soakSmokeSeedPasses(uint64_t seed) {
    soak::RunResult result = soak::runSeed(soakSmokeConfig(seed));
    expectSoakPasses(result);
    EXPECT_GT(result.counts["storedMatches"], 0u) << result.summary;
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
// into a window where every upload fails in transport, then one where every upload
// answers 503
inline void soakSingleCopiesOutliveLostAcksAndServerErrors() {
    soak::RunResult result = playSoakScript(4, 1,
                                            {
                                            "at=1000 visit a=1 b=0 dur=2500",
                                            "at=1000 duel a=0 b=1 cut=0 k=1 rb=300 rh=250",
                                            "at=1000 duel a=2 b=3 rb=-1 rh=200",
                                            "at=3000 ackloss a=- b=- dur=30000",
                                            "at=40000 httperr a=- b=0 dur=20000 mode=2",
                                            "at=41000 fdnreboot a=- b=0 down=1000",
                                            "at=62000 httperr a=- b=0 dur=20000 mode=1",
                                            "at=63000 fdnreboot a=- b=0 down=1000",
                                            });
    EXPECT_GE(result.counts["allowedCutBeforeCommit"], 1u) << result.summary;
    EXPECT_GE(result.counts["storedMatches"], 2u) << result.summary;
    EXPECT_GE(result.counts["syncedStampsChecked"], 1u) << result.summary;
}

// Test: the same seed twice plays the same event, tick for tick
inline void soakSameSeedReplaysIdentically() {
    soak::SoakConfig config = soakSmokeConfig(5);
    config.pdns = 6;
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
    for (const soak::Action& action : soak::Scheduler::generate(config, 1, 8, 2)) {
        soak::Action parsed;
        ASSERT_TRUE(soak::parseAction(soak::formatAction(action), parsed));
        EXPECT_EQ(soak::formatAction(parsed), soak::formatAction(action));
    }
}

// Test: each fault type applies when scheduled, and the cuts and reboots it arms
// fire: a duel cut and reboot, a cut on an idle PDN that then records, an FDN cut
// at its next write, next to a ring and a visit
inline void soakEveryFaultTypeFires() {
    soak::RunResult result = playSoakScript(11, 2,
                                            {
                                            "at=1000 duel a=0 b=1 cut=1 k=1 rb=300 rh=250",
                                            "at=1000 duel a=2 b=3 reboot=0 rebootdelay=100",
                                            "at=1000 ring a=4,6,8 b=- cm=7",
                                            "at=1000 visit a=7 b=0 dur=3000",
                                            "at=2000 reboot a=5 b=- down=1000",
                                            "at=2000 cut a=9 b=- k=1 torn=1",
                                            "at=3000 duel a=10 b=9 rb=200 rh=300",
                                            "at=3000 fdnoff a=- b=0 dur=5000 mode=3",
                                            "at=3000 fdnreboot a=- b=1 down=1000",
                                            "at=4000 fdnreboot a=- b=0 k=1 torn=0",
                                            "at=4000 burst a=- b=- dur=1000 loss=50",
                                            "at=4000 noevent a=- b=- dur=5000",
                                            "at=5000 httperr a=- b=0 dur=5000 mode=1",
                                            "at=30000 ackloss a=- b=- dur=3000",
                                            });
    const std::map<std::string, uint64_t> applied = {
    {"apply_duel", 3},    {"apply_ring", 1},    {"apply_visit", 1},     {"apply_reboot", 1},
    {"apply_cut", 1},     {"apply_fdnoff", 1},  {"apply_fdnreboot", 2}, {"apply_burst", 1},
    {"apply_noevent", 1}, {"apply_httperr", 1}, {"apply_ackloss", 1}};
    for (const std::pair<const std::string, uint64_t>& count : applied)
        EXPECT_EQ(result.counts[count.first], count.second) << count.first;
    EXPECT_EQ(result.counts["cutsArmed"], 3u);
    EXPECT_EQ(result.counts["cutsFired"], 3u);
    EXPECT_EQ(result.counts["pdnCutsTakenDown"], 2u);
    EXPECT_EQ(result.counts["duelRebootsFired"], 1u);
    EXPECT_EQ(result.counts["pdnRebootsFired"], 2u);
    EXPECT_EQ(result.counts["duelRebootsNeverFired"], 0u);
    EXPECT_EQ(result.counts["cutsArmedNeverFired"], 0u);
    EXPECT_EQ(result.counts["actionsSkipped"], 0u);
}

// Test: an FDN's client never associates while offline, fails a request in
// transport only after retrying it once 2 s later, and answers a server error as 503
inline void soakHttpFaultsReachTheFdnClient() {
    uint64_t now = 0;
    soak::VirtualClock clock(now, 0);
    PlatformClock* saved = SimpleTimer::getPlatformClock();
    SimpleTimer::setPlatformClock(&clock);
    soak::FakeServer server(1);
    soak::HttpFaults faults;
    faults.offline = true;
    std::mt19937_64 rng(1);
    std::map<std::string, uint64_t> unconfirmed;
    soak::SimHttpClient client("http", server, faults, rng, unconfirmed);
    std::vector<int> failures;
    const auto request = [&]() {
        client.queueRequest(
        HttpRequest("/", "GET", "", "", nullptr,
                    [&](const WirelessErrorInfo& error) { failures.push_back(error.httpStatus); }));
    };
    const auto run = [&](uint64_t ms) {
        for (const uint64_t end = now + ms; now < end; now += 5)
            client.exec();
    };
    client.setHttpClientState(HttpClientState::CONNECTED);
    run(5000);
    EXPECT_FALSE(client.isConnected());
    faults.offline = false;
    faults.transportFail = 1;
    request();
    run(1000);
    EXPECT_TRUE(failures.empty());
    run(3000);
    EXPECT_EQ(failures, std::vector<int>{0});
    faults.transportFail = 0;
    faults.serverError = 1;
    request();
    run(1000);
    EXPECT_EQ(failures, (std::vector<int>{0, 503}));
    SimpleTimer::setPlatformClock(saved);
}
