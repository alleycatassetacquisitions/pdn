#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "device-mock.hpp"
#include "device-tests.hpp"
#include "game/player.hpp"
#include "utility-tests.hpp"
#include "wireless/remote-player-manager.hpp"

constexpr unsigned long FDN_BEACON_INTERVAL_MS = 12000;

/**
 * A radio whose enabled state the test sets. Like both real drivers it refuses
 * a send while disabled; it counts every player-info beacon it is offered and
 * records the ones it accepts.
 */
class FdnBeaconTests : public testing::Test {
public:
    /** Starts with the peer-to-peer radio enabled and no beacon sent. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(peerComms, getGlobalBroadcastAddress())
        .WillByDefault(testing::Return(MockDevice::BROADCAST_MAC));
        ON_CALL(peerComms,
                sendData(testing::_, PktType::kPlayerInfoBroadcast, testing::_, testing::_))
        .WillByDefault(
        [this](const uint8_t* toMac, PktType, const uint8_t* data, const size_t length) {
            offers++;
            if (radioState != PeerCommsState::ENABLED) return -1;
            sentToBroadcast = memcmp(toMac, MockDevice::BROADCAST_MAC, 6) == 0;
            beacons.emplace_back(data, data + length);
            return 0;
        });

        char playerId[] = "0042";
        player.setUserID(playerId);
    }

    /** Releases the fake clock. */
    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakePlatformClock* fakeClock = nullptr;
    testing::NiceMock<MockPeerComms> peerComms;
    PeerCommsState radioState = PeerCommsState::ENABLED;
    Player player;
    std::vector<std::vector<uint8_t>> beacons;
    bool sentToBroadcast = false;
    size_t offers = 0;
};

inline void fdnBeaconIdentifiesItsSenderAsAnFdn(FdnBeaconTests* suite) {
    RemotePlayerManager fdnBeacon(&suite->peerComms, DeviceType::FDN);
    fdnBeacon.startBroadcastingPlayerInfo(&suite->player, FDN_BEACON_INTERVAL_MS);

    ASSERT_EQ(suite->beacons.size(), 1u);
    EXPECT_TRUE(suite->sentToBroadcast);
    ASSERT_EQ(suite->beacons[0].size(), sizeof(PlayerInfoPkt));
    PlayerInfoPkt fromFdn{};
    memcpy(&fromFdn, suite->beacons[0].data(), sizeof(fromFdn));
    EXPECT_EQ(fromFdn.senderDeviceType, static_cast<uint8_t>(DeviceType::FDN));

    RemotePlayerManager pdnBeacon(&suite->peerComms, DeviceType::PDN);
    pdnBeacon.startBroadcastingPlayerInfo(&suite->player, FDN_BEACON_INTERVAL_MS);
    ASSERT_EQ(suite->beacons.size(), 2u);
    PlayerInfoPkt fromPdn{};
    memcpy(&fromPdn, suite->beacons[1].data(), sizeof(fromPdn));
    EXPECT_NE(fromPdn.senderDeviceType, static_cast<uint8_t>(DeviceType::FDN))
    << "a beacon from any other sender must not read as an FDN's";
}

// SimpleTimer expires once its duration has been exceeded, so the beacon goes
// out on the first tick after 12,000 ms have passed.
inline void fdnBeaconRepeatsEvery12000Ms(FdnBeaconTests* suite) {
    RemotePlayerManager fdnBeacon(&suite->peerComms, DeviceType::FDN);
    fdnBeacon.startBroadcastingPlayerInfo(&suite->player, FDN_BEACON_INTERVAL_MS);
    ASSERT_EQ(suite->beacons.size(), 1u);

    suite->fakeClock->advance(FDN_BEACON_INTERVAL_MS);
    fdnBeacon.update();
    EXPECT_EQ(suite->beacons.size(), 1u);

    suite->fakeClock->advance(1);
    fdnBeacon.update();
    EXPECT_EQ(suite->beacons.size(), 2u);

    fdnBeacon.update();
    EXPECT_EQ(suite->beacons.size(), 2u) << "one beacon per interval, not per tick";
}

// What a device shows around an HTTP upload: the beacon that fell due while
// the radio was disabled goes out soon after the radio is enabled again.
inline void fdnBeaconRefusedWhileTheRadioIsDisabledGoesOutOnceItIsEnabled(FdnBeaconTests* suite) {
    RemotePlayerManager fdnBeacon(&suite->peerComms, DeviceType::FDN);
    fdnBeacon.startBroadcastingPlayerInfo(&suite->player, FDN_BEACON_INTERVAL_MS);
    ASSERT_EQ(suite->beacons.size(), 1u);

    suite->radioState = PeerCommsState::DISABLED;
    suite->fakeClock->advance(FDN_BEACON_INTERVAL_MS + 1);
    fdnBeacon.update();
    ASSERT_EQ(suite->offers, 2u) << "the due beacon was not offered";

    fdnBeacon.update();
    suite->fakeClock->advance(BEACON_RETRY_AFTER_REFUSAL_MS);
    fdnBeacon.update();
    EXPECT_EQ(suite->offers, 2u) << "a refused beacon was offered again before its retry wait";

    suite->radioState = PeerCommsState::ENABLED;
    suite->fakeClock->advance(1);
    fdnBeacon.update();
    EXPECT_EQ(suite->beacons.size(), 2u)
    << "the refused beacon was counted as sent and waits a whole interval";

    // The interval runs from the beacon that went out, not from the refused one.
    suite->fakeClock->advance(FDN_BEACON_INTERVAL_MS);
    fdnBeacon.update();
    EXPECT_EQ(suite->beacons.size(), 2u);
    suite->fakeClock->advance(1);
    fdnBeacon.update();
    EXPECT_EQ(suite->beacons.size(), 3u);
}

// The FDN's main pumps the beacon from the device tick. Only the mounted app's
// state loops, so a beacon pumped from a state stops when another app mounts.
inline void fdnBeaconRunsWhicheverAppIsMounted(AppSwapTestSuite* suite) {
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    clock.setTime(1000);

    int beacons = 0;
    ON_CALL(*suite->device->mockPeerComms,
            sendData(testing::_, PktType::kPlayerInfoBroadcast, testing::_, testing::_))
    .WillByDefault([&beacons](const uint8_t*, PktType, const uint8_t*, const size_t) {
        beacons++;
        return 0;
    });

    Player fdnPlayer;
    char playerId[] = "0042";
    fdnPlayer.setUserID(playerId);
    RemotePlayerManager remotePlayerManager(suite->device->mockPeerComms, DeviceType::FDN);
    suite->device->setTickCallback([&remotePlayerManager]() { remotePlayerManager.update(); });
    suite->loadAllApps(APP_ONE);
    remotePlayerManager.startBroadcastingPlayerInfo(&fdnPlayer, FDN_BEACON_INTERVAL_MS);
    // EXPECT throughout: an early return would skip the cleanup at the end.
    EXPECT_EQ(beacons, 1);

    suite->device->setActiveApp(APP_TWO);
    EXPECT_EQ(suite->device->getActiveApp(), suite->appTwo);
    clock.advance(FDN_BEACON_INTERVAL_MS + 1);
    suite->device->loop();
    EXPECT_EQ(beacons, 2) << "the beacon stopped when the home app was swapped out";

    suite->device->setTickCallback(nullptr);
    SimpleTimer::setPlatformClock(nullptr);
}
