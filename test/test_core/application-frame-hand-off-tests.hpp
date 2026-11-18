#pragma once

#include <gtest/gtest.h>

#include "device/application-frame-hand-off.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

// ============================================
// Application frame hand-off (main loop -> HELLO writer)
// ============================================

constexpr uint8_t TEST_KIND_A = 0x10;
constexpr uint8_t TEST_KIND_B = 0x11;
constexpr uint8_t TEST_KIND_UNREGISTERED = 0x12;

inline std::vector<uint8_t> applicationFrameBytes(uint8_t kind, const std::vector<uint8_t>& bytes) {
    ApplicationFrame frame{};
    const size_t used = encodeApplicationFrame(kind, bytes.data(), bytes.size(), frame);
    return std::vector<uint8_t>(frame.begin(), frame.begin() + used);
}

// Spins until `reached` or five seconds pass, so a broken hand-off fails
// rather than hangs.
inline bool spinUntil(const std::function<bool()>& reached) {
    const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!reached()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

// The two sides on two real threads, as on the ESP32: the main loop offers
// frames and either waits for each to be written or races the writer to drop
// it. A hand-off that let both hold the buffer shows up as a torn frame here
// and as a data race under ThreadSanitizer, and every frame the writer
// finished is reported WRITTEN exactly once.
inline void applicationFrameHandOffKeepsFramesWholeAcrossTwoThreads() {
    constexpr uint32_t FRAME_COUNT = 5000;
    auto frameOf = [](uint32_t sequence) {
        uint8_t payload[sizeof(sequence)];
        std::memcpy(payload, &sequence, sizeof(sequence));
        return applicationFrameBytes(TEST_KIND_A, std::vector<uint8_t>(payload, payload + 4));
    };
    ApplicationFrameHandOff handOff;
    std::atomic<bool> offeringDone{false};

    // The writer's alone until it is joined.
    size_t finishedCount = 0;
    size_t tornCount = 0;
    std::thread writer([&]() {
        while (!offeringDone.load()) {
            size_t length = 0;
            const uint8_t* frame = handOff.takeForWrite(length);
            if (frame == nullptr) {
                std::this_thread::yield();
                continue;
            }
            const std::vector<uint8_t> copy(frame, frame + length);
            handOff.finish();
            uint32_t sequence = 0;
            if (copy.size() > 5 + sizeof(sequence)) std::memcpy(&sequence, &copy[5], 4);
            ++(copy == frameOf(sequence) ? finishedCount : tornCount);
        }
    });

    size_t reportedWrittenCount = 0;
    size_t droppedCount = 0;
    bool stalled = false;
    for (uint32_t sequence = 0; sequence < FRAME_COUNT && !stalled; ++sequence) {
        uint8_t payload[sizeof(sequence)];
        std::memcpy(payload, &sequence, sizeof(sequence));
        stalled = !spinUntil([&] { return handOff.offer(TEST_KIND_A, payload, sizeof(payload)); });
        if (stalled) break;
        if (sequence % 2 == 0) {
            stalled = !spinUntil([&] { return handOff.takeWritten(); });
            ++reportedWrittenCount;
            continue;
        }
        ApplicationFrameOutcome outcome = ApplicationFrameOutcome::DROPPED;
        // A frame offered and not yet reported is always settled by drop().
        stalled = !handOff.drop(outcome);
        ++(outcome == ApplicationFrameOutcome::WRITTEN ? reportedWrittenCount : droppedCount);
    }
    offeringDone.store(true);
    writer.join();

    ASSERT_FALSE(stalled);
    EXPECT_EQ(tornCount, 0u);
    EXPECT_EQ(finishedCount, reportedWrittenCount);
    EXPECT_EQ(reportedWrittenCount + droppedCount, FRAME_COUNT);
}
