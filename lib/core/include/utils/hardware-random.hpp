#pragma once

#include <cstddef>
#include <cstdint>

#ifdef NATIVE_BUILD
#include <random>
#else
#include <esp_random.h>
#endif

// The hardware RNG, for bytes nobody may predict: keys and seeds. IdGenerator
// is a seeded PRNG and is not a substitute. On the ESP32 the output is true
// random only while the radio (or the bootloader's entropy source) is on; every
// caller runs with ESP-NOW up.

/** Fills @p length bytes at @p buffer from the hardware RNG. */
inline void fillHardwareRandom(uint8_t* buffer, size_t length) {
#ifdef NATIVE_BUILD
    std::random_device device;
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<uint8_t>(device());
    }
#else
    esp_fill_random(buffer, length);
#endif
}

/** One 32-bit word from the hardware RNG. */
inline uint32_t hardwareRandomWord() {
#ifdef NATIVE_BUILD
    return std::random_device{}();
#else
    return esp_random();
#endif
}
