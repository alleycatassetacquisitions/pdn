#pragma once

#include <cstddef>
#include <cstdint>

/**
 * True when the @p length bytes at @p a and @p b are equal. Every byte is
 * compared whatever the first difference, so the time taken says nothing about
 * how much of a forged tag or digest was right.
 */
inline bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t length) {
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return difference == 0;
}
