#pragma once

#include <cstdint>

constexpr uint8_t FDN_BOX_ID = 0;
constexpr size_t PLAYER_ID_LENGTH = 4;
constexpr size_t PLAYER_ID_BUFFER_SIZE = PLAYER_ID_LENGTH + 1; // 4 digits + null terminator
