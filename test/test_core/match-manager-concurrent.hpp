#pragma once

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
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
    FakeQuickdrawWirelessManager wireless;
    FakeRemoteDeviceCoordinator rdc;
    const uint8_t peerMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    rdc.setPeerMac(SerialIdentifier::INPUT_JACK, peerMac);

    NativePeerCommsDriver driver("tsan-test-driver");
    driver.initialize();
    driver.connect();

    MatchManager mm;
    mm.initialize(&player, &storage, &wireless);
    mm.setRemoteDeviceCoordinator(&rdc);
    mm.clearCurrentMatch();

    // Handler is invoked by exec() on the main thread — never by the WiFi thread.
    driver.setPacketHandler(
        PktType::kQuickdrawCommand,
        [](const uint8_t* src, const uint8_t* data, size_t len, void* ctx) {
            if (len < sizeof(QuickdrawPacket)) return;
            auto* matchMgr = static_cast<MatchManager*>(ctx);
            const auto* pkt = reinterpret_cast<const QuickdrawPacket*>(data);
            QuickdrawCommand cmd(src,
                                 static_cast<QDCommand>(pkt->command),
                                 pkt->matchId, pkt->playerId,
                                 pkt->playerDrawTime, pkt->isHunter);
            matchMgr->listenForMatchEvents(cmd);
        },
        &mm
    );

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
            pkt.command        = static_cast<int>(QDCommand::SEND_MATCH_ID);
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
    while (std::chrono::steady_clock::now() < deadline) {
        driver.exec();
        sideEffect += static_cast<int>(mm.isMatchReady());
        sideEffect += static_cast<int>(mm.getHasReceivedDrawResult());
        sideEffect += static_cast<int>(mm.getHasPressedButton());
        if (mm.getCurrentMatch().has_value()) {
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

    SUCCEED() << "No TSan races expected: MatchManager is only accessed from exec() on the main thread";
}
