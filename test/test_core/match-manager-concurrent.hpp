#pragma once

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "wireless/quickdraw-packet.hpp"
#include "utils/simple-timer.hpp"
#include "device-mock.hpp"
#include "utility-tests.hpp"

// Verifies that the queue-based deferral in NativePeerCommsDriver eliminates the
// race between the simulated WiFi task (receivePacket) and the main loop (exec +
// MatchManager reads).
//
// Before the fix: receivePacket() called listenForMatchEvents() directly, racing
// with the main loop reading isMatchReady() / getHunterDrawTime() etc.
//
// After the fix: receivePacket() only enqueues; exec() drains on the caller's
// thread. MatchManager is therefore only ever touched from a single thread.
//
// Run under TSan to confirm zero race reports:
//   pio test -e native_tsan -f test_core --filter "*MatchManagerConcurrent*"
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
    driver.connect();

    // A real WirelessManager, so constructing MatchManager claims the duel
    // channel and the channel installs the receive handler. Built with nullptr the
    // channel installs nothing, and this test would drive a decode trampoline of
    // its own instead of the path the firmware runs. Declared before mm so it
    // outlives the channel that points at it.
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

    // No handler is armed here: the duel channel claimed kQuickdrawCommand when mm
    // was constructed, and the driver invokes it from exec() on this thread.

    std::atomic<bool> running{true};

    // Simulates the WiFi task: only enqueues after the fix — never touches MatchManager.
    std::thread wifiTask([&]() {
        int iter = 0;
        while (running.load(std::memory_order_relaxed)) {
            char matchId[37] = {};
            snprintf(matchId, sizeof(matchId), "match-%08d", iter++);

            QuickdrawPacket pkt = {};
            strncpy(pkt.matchId,  matchId, sizeof(pkt.matchId) - 1);
            strncpy(pkt.playerId, "hunt",  sizeof(pkt.playerId) - 1);
            pkt.command        = QDCommand::SEND_MATCH_ID;
            pkt.isHunter       = true;
            pkt.playerDrawTime = 0;

            driver.receivePacket(peerMac, PktType::kQuickdrawCommand,
                                 reinterpret_cast<const uint8_t*>(&pkt),
                                 sizeof(pkt));
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

    // Without this the test is a TSan vehicle that passes whether or not a frame
    // ever arrived, which is how it went green while the channel installed no
    // handler at all.
    EXPECT_TRUE(reachedTheManager)
        << "no SEND_MATCH_ID reached MatchManager through the duel channel";
    SUCCEED() << "No TSan races expected: MatchManager is only accessed from exec() on the main thread";
}
