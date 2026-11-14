#pragma once

#include <gtest/gtest.h>

#include "device/application-frame-hand-off.hpp"
#include "device/serial-frame-parser.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

// ============================================
// Application frame hand-off (main loop -> HELLO writer)
// ============================================

// A payload that names its own sequence number in its first four bytes and
// derives every other byte from it, so a frame mixed from two offers, or read
// while it is being cleared, matches no sequence number.
inline ApplicationPayload sequencedPayload(uint32_t sequence) {
    ApplicationPayload payload{};
    for (size_t i = 0; i < payload.size(); ++i) {
        const uint8_t sequenceByte = static_cast<uint8_t>(sequence >> (8 * (i % 4)));
        payload[i] = i < 4 ? sequenceByte : static_cast<uint8_t>(sequenceByte ^ (i * 31));
    }
    return payload;
}

inline ApplicationFrame framed(const ApplicationPayload& payload) {
    ApplicationFrame frame{};
    encodeApplicationFrame(payload, frame);
    return frame;
}

// True when `frame` is, byte for byte, the frame of one sequenced payload;
// `sequence` is then the one it names.
inline bool isWholeSequencedFrame(const ApplicationFrame& frame, uint32_t& sequence) {
    sequence = 0;
    for (size_t i = 0; i < 4; ++i) {
        sequence |= static_cast<uint32_t>(frame[3 + i]) << (8 * i);
    }
    return frame == framed(sequencedPayload(sequence));
}

inline bool isAllZero(const ApplicationFrame& frame) {
    return std::all_of(frame.begin(), frame.end(), [](uint8_t b) { return b == 0; });
}

inline void applicationFrameHandOffGivesTheWriterTheOfferedFrameOnce() {
    ApplicationFrameHandOff handOff;
    const ApplicationPayload payload = sequencedPayload(7);

    EXPECT_EQ(handOff.takeForWrite(), nullptr) << "an empty slot handed out a frame";
    ASSERT_TRUE(handOff.offer(payload));

    const ApplicationFrame* frame = handOff.takeForWrite();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(*frame, framed(payload));
    EXPECT_EQ(handOff.takeForWrite(), nullptr) << "the same frame was handed out twice";
    EXPECT_FALSE(handOff.takeWritten()) << "reported written before the writer finished";

    handOff.finish();
    EXPECT_TRUE(isAllZero(*frame)) << "the payload outlived its write";
    EXPECT_TRUE(handOff.takeWritten());
    EXPECT_FALSE(handOff.takeWritten()) << "one frame was reported written twice";
    EXPECT_EQ(handOff.takeForWrite(), nullptr);
}

inline void applicationFrameHandOffHoldsOneFrameAtATime() {
    ApplicationFrameHandOff handOff;
    const ApplicationPayload first = sequencedPayload(1);
    const ApplicationPayload second = sequencedPayload(2);

    ASSERT_TRUE(handOff.offer(first));
    EXPECT_FALSE(handOff.offer(second)) << "accepted while the first was waiting";

    const ApplicationFrame* frame = handOff.takeForWrite();
    ASSERT_NE(frame, nullptr);
    EXPECT_FALSE(handOff.offer(second)) << "accepted while the first was being written";
    EXPECT_EQ(*frame, framed(first)) << "a refused offer wrote over the frame in hand";

    handOff.finish();
    EXPECT_FALSE(handOff.offer(second)) << "accepted before the first was collected";

    ASSERT_TRUE(handOff.takeWritten());
    ASSERT_TRUE(handOff.offer(second));
    frame = handOff.takeForWrite();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(*frame, framed(second));
}

inline void applicationFrameHandOffDropTakesBackAFrameTheWriterHasNotTaken() {
    ApplicationFrameHandOff handOff;

    // One full pass first, only to learn where the slot keeps its frame.
    ASSERT_TRUE(handOff.offer(sequencedPayload(1)));
    const ApplicationFrame* buffer = handOff.takeForWrite();
    ASSERT_NE(buffer, nullptr);
    handOff.finish();
    ASSERT_TRUE(handOff.takeWritten());

    ASSERT_TRUE(handOff.offer(sequencedPayload(2)));
    ASSERT_FALSE(isAllZero(*buffer)) << "the check below would pass on an unfilled buffer";

    ApplicationFrameOutcome outcome = ApplicationFrameOutcome::WRITTEN;
    ASSERT_TRUE(handOff.drop(outcome));
    EXPECT_EQ(outcome, ApplicationFrameOutcome::DROPPED);
    EXPECT_TRUE(isAllZero(*buffer)) << "the payload outlived its drop";
    EXPECT_EQ(handOff.takeForWrite(), nullptr) << "a dropped frame was still handed out";
    EXPECT_FALSE(handOff.takeWritten());
    EXPECT_FALSE(handOff.drop(outcome)) << "one frame was settled twice";

    EXPECT_TRUE(handOff.offer(sequencedPayload(3)));
}

// The writer already holds the frame: it cannot be recalled, so it counts as
// written from then on, and the writer's copy is not cleared under it.
inline void applicationFrameHandOffDropAfterTheWriterTookTheFrameSettlesItWritten() {
    ApplicationFrameHandOff handOff;
    const ApplicationPayload payload = sequencedPayload(5);
    ASSERT_TRUE(handOff.offer(payload));
    const ApplicationFrame* frame = handOff.takeForWrite();
    ASSERT_NE(frame, nullptr);

    ApplicationFrameOutcome outcome = ApplicationFrameOutcome::DROPPED;
    ASSERT_TRUE(handOff.drop(outcome));
    EXPECT_EQ(outcome, ApplicationFrameOutcome::WRITTEN);
    EXPECT_EQ(*frame, framed(payload)) << "the frame changed under the writer";
    EXPECT_FALSE(handOff.drop(outcome)) << "one frame was settled twice";
    EXPECT_FALSE(handOff.offer(sequencedPayload(6))) << "accepted while the writer held the slot";

    handOff.finish();
    EXPECT_TRUE(isAllZero(*frame));
    EXPECT_FALSE(handOff.takeWritten()) << "a settled frame was reported again";
    EXPECT_FALSE(handOff.drop(outcome));
    EXPECT_TRUE(handOff.offer(sequencedPayload(6)));
}

inline void applicationFrameHandOffDropAfterFinishSettlesItWrittenOnce() {
    ApplicationFrameHandOff handOff;
    ASSERT_TRUE(handOff.offer(sequencedPayload(5)));
    ASSERT_NE(handOff.takeForWrite(), nullptr);
    handOff.finish();

    ApplicationFrameOutcome outcome = ApplicationFrameOutcome::DROPPED;
    ASSERT_TRUE(handOff.drop(outcome));
    EXPECT_EQ(outcome, ApplicationFrameOutcome::WRITTEN);
    EXPECT_FALSE(handOff.takeWritten()) << "a settled frame was reported again";
    EXPECT_FALSE(handOff.drop(outcome));
    EXPECT_TRUE(handOff.offer(sequencedPayload(6)));
}

inline void applicationFrameHandOffEmptySlotSettlesNothing() {
    ApplicationFrameHandOff handOff;
    ApplicationFrameOutcome outcome = ApplicationFrameOutcome::WRITTEN;
    EXPECT_FALSE(handOff.drop(outcome));
    EXPECT_EQ(outcome, ApplicationFrameOutcome::WRITTEN) << "an outcome was invented";
    EXPECT_FALSE(handOff.takeWritten());
}

// Yields until `reached()` holds. False if it still does not after five
// seconds, so a hand-off that never gets there fails its test instead of
// hanging the run; a working one gets there in microseconds.
inline bool yieldUntilReached(const std::function<bool()>& reached) {
    const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!reached()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

// The two sides on two real threads, as on the ESP32. One offers every frame
// and then either waits for it to be written or races the writer to drop it;
// the other takes, copies out byte by byte as the jack write does, and
// finishes. The writer reads the very buffer the offering side fills and
// clears, so a hand-off that let both hold it at once shows up here as a torn
// frame and under ThreadSanitizer as a data race. Every wait is bounded and
// the writer leaves on the flag alone, so a broken hand-off fails, not hangs.
inline void applicationFrameHandOffKeepsEveryFrameWholeAcrossTwoThreads() {
    constexpr uint32_t FRAME_COUNT = 20000;
    ApplicationFrameHandOff handOff;
    std::atomic<bool> offeringDone{false};

    // The writer's alone until it is joined.
    std::vector<uint32_t> writtenSequences;
    uint32_t tornFrameCount = 0;
    uint32_t excessWriteCount = 0;
    std::thread writer([&]() {
        // Every frame is settled before the flag is set, and one this thread
        // already holds is finished before the flag is read again.
        while (!offeringDone.load()) {
            const ApplicationFrame* frame = handOff.takeForWrite();
            if (frame == nullptr) {
                std::this_thread::yield();
                continue;
            }
            ApplicationFrame copy{};
            for (size_t i = 0; i < copy.size(); ++i) {
                copy[i] = (*frame)[i];
            }
            handOff.finish();
            uint32_t sequence = 0;
            if (!isWholeSequencedFrame(copy, sequence)) {
                ++tornFrameCount;
            } else if (writtenSequences.size() < FRAME_COUNT) {
                writtenSequences.push_back(sequence);
            } else {
                ++excessWriteCount;
            }
        }
    });

    std::vector<uint32_t> reportedWritten;
    uint32_t droppedCount = 0;
    uint32_t unsettledCount = 0;
    bool stalled = false;
    for (uint32_t sequence = 0; sequence < FRAME_COUNT && !stalled; ++sequence) {
        const ApplicationPayload payload = sequencedPayload(sequence);
        // Refused only while the writer still holds a frame drop() settled.
        if (!yieldUntilReached([&]() { return handOff.offer(payload); })) {
            stalled = true;
        } else if (sequence % 3 == 0) {
            if (yieldUntilReached([&]() { return handOff.takeWritten(); })) {
                reportedWritten.push_back(sequence);
            } else {
                stalled = true;
            }
        } else {
            // Straight away the drop usually wins; after a yield the writer
            // usually has the frame or has finished it.
            if (sequence % 3 == 2) std::this_thread::yield();
            ApplicationFrameOutcome outcome = ApplicationFrameOutcome::DROPPED;
            if (!handOff.drop(outcome)) {
                ++unsettledCount;
            } else if (outcome == ApplicationFrameOutcome::WRITTEN) {
                reportedWritten.push_back(sequence);
            } else {
                ++droppedCount;
            }
        }
    }
    offeringDone.store(true);
    writer.join();

    ASSERT_FALSE(stalled) << "the hand-off never took another frame or never finished one";
    EXPECT_EQ(tornFrameCount, 0u);
    EXPECT_EQ(excessWriteCount, 0u) << "more frames were written than were ever offered";
    EXPECT_EQ(unsettledCount, 0u) << "an accepted frame ended with no outcome";
    EXPECT_EQ(reportedWritten.size() + droppedCount, FRAME_COUNT);
    // In order and equal: nothing written twice, nothing written after it was
    // reported dropped, nothing reported written that never was.
    EXPECT_EQ(writtenSequences, reportedWritten);
}
