#pragma once

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "wireless/match-packet.hpp"
#include "utils/simple-timer.hpp"
#include "device-mock.hpp"
#include "utility-tests.hpp"

// receivePacket() enqueues while exec() drains on the caller's thread so MatchManager is single-threaded under TSan.
inline void matchManagerConcurrentDriverVsReader() {
    FakePlatformClock clock;
    clock.setTime(1000);
    SimpleTimer::setPlatformClock(&clock);

    // Declare dependencies before MatchManager so they outlive it.
    // C++ destroys locals in reverse declaration order, so anything mm's
    // destructor touches (storage->end(), etc.) must be declared earlier.
    MockStorage storage;
    Player player;
    char playerId[] = "test";
    player.setUserID(playerId);
    player.setIsHunter(false);
    FakeRemoteDeviceCoordinator rdc;
    const uint8_t peerMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    rdc.setPeerMac(SerialIdentifier::INPUT_JACK, peerMac);

    NativePeerCommsDriver driver("tsan-test-driver");
    driver.initialize();
    driver.enable();

    // Declared before mm so wireless outlives mm's matchChannel destructor.
    WirelessManager wireless(&driver, nullptr);
    MatchManager mm(&wireless);
    using ::testing::_;
    ON_CALL(storage, write(_, _, _))
        .WillByDefault([](const std::string&, const std::string&, const std::string& value) {
            return value.size();
        });
    ON_CALL(storage, writeUChar(_, _, _)).WillByDefault(::testing::Return(1));
    ON_CALL(storage, readUChar(_, _, _)).WillByDefault(::testing::Return(0));
    mm.initialize(&player, &storage);
    mm.setRemoteDeviceCoordinator(&rdc);
    mm.clearCurrentMatch();

    std::atomic<bool> running{true};

    // The secret is on the cable first; the WiFi task repeats the initiation it signs.
    const char* matchId = "aaaaaaaa-0000-4000-8000-000000000001";
    const std::vector<uint8_t> secretFrame = matchSecretFrame(matchId, testMatchSecret(1));
    mm.onMatchSecretFrame(SerialIdentifier::INPUT_JACK, secretFrame.data(), secretFrame.size());
    MatchFrameAuth hunterAuth(testMatchSecret(1));
    const MatchPacket initiation =
    signedMatchFrame(hunterAuth, MatchCmd::SEND_MATCH_ID, matchId, true);

    // Simulates the WiFi task: only enqueues after the fix — never touches MatchManager.
    std::thread wifiTask([&]() {
        while (running.load(std::memory_order_relaxed)) {
            driver.receivePacket(peerMac, PktType::kMatchCommand,
                                 reinterpret_cast<const uint8_t*>(&initiation), sizeof(initiation));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Simulates the main loop: drains the queue (which calls listenForMatchEvents
    // on this thread), then reads MatchManager state — all on one thread.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    volatile int sideEffect = 0;
    bool reachedTheManager = false;
    while (std::chrono::steady_clock::now() < deadline) {
        driver.exec();
        sideEffect += static_cast<int>(mm.isMatchReady());
        sideEffect += static_cast<int>(mm.getHasReceivedDrawResult());
        sideEffect += static_cast<int>(mm.getHasPressedButton());
        if (mm.getCurrentMatch().has_value()) {
            reachedTheManager = true;
            sideEffect += static_cast<int>(mm.getCurrentMatch()->getHunterDrawTime());
            sideEffect += static_cast<int>(mm.getCurrentMatch()->getBountyDrawTime());
        }
        std::this_thread::yield();
    }
    (void)sideEffect;

    running.store(false);
    wifiTask.join();
    mm.clearCurrentMatch();
    SimpleTimer::setPlatformClock(nullptr);

    EXPECT_TRUE(reachedTheManager)
    << "no SEND_MATCH_ID reached MatchManager through the match channel";
    SUCCEED() << "No TSan races expected: MatchManager is only accessed from exec() on the main thread";
}
