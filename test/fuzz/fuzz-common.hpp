#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "device/drivers/platform-clock.hpp"
#include "game/match.hpp"
#include "id-generator.hpp"

class FuzzStepClock : public PlatformClock {
public:
    /** Advances the synthetic clock by deltaMs milliseconds. */
    void advance(unsigned long deltaMs) { currentTimeMs += deltaMs; }

    /** Returns the current synthetic clock reading in milliseconds. */
    unsigned long milliseconds() override { return currentTimeMs; }

private:
    unsigned long currentTimeMs = 1000;
};

// Exact-sized heap allocation so ASan places a redzone immediately at data() + size().
class ExactBuffer {
public:
    /** Copies len bytes from src into a tight heap allocation. */
    ExactBuffer(const uint8_t* src, size_t len)
        : buf(new uint8_t[len])
        , bufSize(len) {
        if (len > 0 && src != nullptr) {
            std::memcpy(buf, src, len);
        }
    }

    /** Releases the heap allocation. */
    ~ExactBuffer() { delete[] buf; }

    /** Non-copyable to prevent double-free of the heap buffer. */
    ExactBuffer(const ExactBuffer&) = delete;
    /** Non-copy-assignable to prevent double-free of the heap buffer. */
    ExactBuffer& operator=(const ExactBuffer&) = delete;

    /** Returns the heap buffer pointer. */
    uint8_t* data() const { return buf; }

    /** Returns the byte length of the buffer. */
    size_t size() const { return bufSize; }

private:
    uint8_t* buf;
    size_t bufSize;
};

/** Traps if the match id text is not null-terminated within its buffer. */
inline void assertMatchStringBounds(const Match& match) {
    if (strnlen(match.getMatchId(), IdGenerator::UUID_BUFFER_SIZE) >=
        IdGenerator::UUID_BUFFER_SIZE) {
        __builtin_trap();
    }
}
