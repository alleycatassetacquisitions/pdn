#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Zeroes @p length bytes at @p data through a volatile pointer, so the stores
 * survive where the compiler could drop a plain one as dead.
 */
inline void secureWipe(void* data, size_t length) {
    volatile uint8_t* bytes = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = 0;
    }
}
