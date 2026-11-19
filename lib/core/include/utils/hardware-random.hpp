#pragma once

#include <cstddef>
#include <cstdint>

// The hardware RNG, for bytes nobody may predict, such as a match secret.
// IdGenerator is a seeded PRNG and is no substitute. On the ESP32 the output is
// true random only while the radio is on; every caller runs with ESP-NOW up.

/** Fills @p length bytes at @p buffer from the hardware RNG. */
void fillHardwareRandom(uint8_t* buffer, size_t length);

#ifdef NATIVE_BUILD
/** Native builds: writes @p length bytes at @p buffer in place of the hardware RNG. */
using HardwareRandomSource = void (*)(uint8_t* buffer, size_t length);

/**
 * Native builds: draws from @p source instead of std::random_device, so a test
 * knows the bytes; nullptr restores std::random_device.
 */
void setHardwareRandomSource(HardwareRandomSource source);
#endif
