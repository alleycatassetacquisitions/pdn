#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "device/driver-names.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/pdn.hpp"
#include "soak/sim-medium.hpp"
#include "soak/soak-hooks.hpp"
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

// A World the test drives by hand: it ticks, and each PDN in MATCH presses once,
// PDN i 200 + 100 * i ms after its draw started.
struct SoakDuelWorld : soak::World {
    using World::cables;
    using World::fdns;
    using World::pdns;
    using World::server;
    using World::setUp;
    using World::World;

    /** Ticks until @p done holds, at most @p ms of virtual time; false if it never did. */
    bool runUntil(const std::function<bool()>& done, uint64_t ms) {
        for (const uint64_t end = now + ms; now < end;) {
            tick();
            for (size_t i = 0; i < pdns.size(); i++) {
                MatchManager* matchManager = pdns[i].device->context().matchManager;
                if (pdns[i].device->stateId() == MATCH && !matchManager->getHasPressedButton() &&
                    pdns[i].clock->milliseconds() - matchManager->getMatchLocalStartTime() >=
                    200 + 100 * i) {
                    pdns[i].device->press();
                }
            }
            if (done()) return true;
        }
        return false;
    }
};

// Test: two PDNs duel on a cable, an FDN collects both records over the medium
// and uploads them, and the fake server holds one accepted row from two reports
inline void soakFdnUploadsACabledDuel() {
    soak::SoakConfig config;
    config.pdns = 2;
    config.fdns = 1;
    SoakDuelWorld world(config);
    ASSERT_TRUE(world.setUp());
    const auto inState = [&world](std::set<int> states) {
        return states.count(world.pdns[0].device->stateId()) != 0 &&
               states.count(world.pdns[1].device->stateId()) != 0;
    };
    ASSERT_TRUE(world.runUntil([&] { return inState({IDLE}); }, 30000));
    ASSERT_TRUE(
    world.cables.plug({0, SerialIdentifier::OUTPUT_JACK}, {1, SerialIdentifier::INPUT_JACK}));
    ASSERT_TRUE(world.runUntil([&] { return inState({WIN, LOSE}); }, 30000));
    world.cables.unplug({0, SerialIdentifier::OUTPUT_JACK});
    // The FDN retires both records once the server answers their upload.
    EXPECT_TRUE(world.runUntil(
    [&] { return world.fdns[0].nvs.getUInt(FDN_RECORD_STORE_NAMESPACE, "m_head", 0) == 2; },
    400000));
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
