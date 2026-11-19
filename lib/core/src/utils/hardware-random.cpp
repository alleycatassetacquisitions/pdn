#include "utils/hardware-random.hpp"

#ifdef NATIVE_BUILD
#include <random>

namespace {
HardwareRandomSource replacementSource = nullptr;
}

void setHardwareRandomSource(HardwareRandomSource source) { replacementSource = source; }

void fillHardwareRandom(uint8_t* buffer, size_t length) {
    if (replacementSource != nullptr) {
        replacementSource(buffer, length);
        return;
    }
    std::random_device device;
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<uint8_t>(device());
    }
}
#else
#include <esp_random.h>

void fillHardwareRandom(uint8_t* buffer, size_t length) { esp_fill_random(buffer, length); }
#endif
