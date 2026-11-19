#pragma once

#include <gtest/gtest.h>

#include "device/application-frame-hand-off.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

// ============================================
// Application frame hand-off (main loop -> HELLO writer)
// ============================================

inline ApplicationFrameHeader applicationFrameHeader(uint8_t kind, const uint8_t* source,
                                                     size_t length) {
    ApplicationFrameHeader header{};
    header.kind = kind;
    memcpy(header.source, source, sizeof(header.source));
    header.length = static_cast<uint8_t>(length);
    return header;
}

/** The wire bytes of one application frame of `kind` from `source`. */
inline std::vector<uint8_t> applicationFrameBytes(uint8_t kind, const uint8_t* source,
                                                  const std::vector<uint8_t>& bytes) {
    ApplicationFrame frame{};
    const size_t used =
    encodeApplicationFrame(applicationFrameHeader(kind, source, bytes.size()), bytes.data(), frame);
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
// finished is reported WRITTEN exactly once, with its kind.
inline void applicationFrameHandOffKeepsFramesWholeAcrossTwoThreads() {
    constexpr uint32_t FRAME_COUNT = 5000;
    constexpr uint8_t KIND = static_cast<uint8_t>(ApplicationFrameKind::MATCH_SECRET);
    const uint8_t source[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    auto frameOf = [&](uint32_t sequence) {
        uint8_t payload[sizeof(sequence)];
        memcpy(payload, &sequence, sizeof(sequence));
        return applicationFrameBytes(KIND, source, std::vector<uint8_t>(payload, payload + 4));
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
            constexpr size_t SEQUENCE_AT = 3 + APPLICATION_HEADER_LEN;
            uint32_t sequence = 0;
            if (copy.size() >= SEQUENCE_AT + sizeof(sequence)) {
                memcpy(&sequence, &copy[SEQUENCE_AT], sizeof(sequence));
            }
            ++(copy == frameOf(sequence) ? finishedCount : tornCount);
        }
    });

    size_t reportedWrittenCount = 0;
    size_t droppedCount = 0;
    size_t wrongKindCount = 0;
    bool stalled = false;
    for (uint32_t sequence = 0; sequence < FRAME_COUNT && !stalled; ++sequence) {
        uint8_t payload[sizeof(sequence)];
        memcpy(payload, &sequence, sizeof(sequence));
        const ApplicationFrameHeader header = applicationFrameHeader(KIND, source, sizeof(payload));
        stalled = !spinUntil([&] { return handOff.offer(header, payload); });
        if (stalled) break;
        std::optional<SettledApplicationFrame> settled;
        if (sequence % 2 == 0) {
            stalled = !spinUntil([&] { return (settled = handOff.takeWritten()).has_value(); });
        } else {
            // A frame offered and not yet reported is always settled by drop().
            settled = handOff.drop();
            stalled = !settled.has_value();
        }
        if (stalled) break;
        if (settled->kind != ApplicationFrameKind::MATCH_SECRET) ++wrongKindCount;
        ++(settled->outcome == ApplicationFrameOutcome::WRITTEN ? reportedWrittenCount
                                                                : droppedCount);
    }
    offeringDone.store(true);
    writer.join();

    ASSERT_FALSE(stalled);
    EXPECT_EQ(tornCount, 0u);
    EXPECT_EQ(wrongKindCount, 0u);
    EXPECT_EQ(finishedCount, reportedWrittenCount);
    EXPECT_EQ(reportedWrittenCount + droppedCount, FRAME_COUNT);
}
