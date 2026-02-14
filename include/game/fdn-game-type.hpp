#pragma once
#include <cstdint>

enum class FdnGameType : uint8_t {
    SIGNAL_ECHO = 0,
    GHOST_RUNNER = 1,
    SPIKE_VECTOR = 2,
    FIREWALL_DECRYPT = 3,
    CIPHER_PATH = 4,
    EXPLOIT_SEQUENCER = 5,
    BREACH_DEFENSE = 6,
    KONAMI_CODE = 7
};
