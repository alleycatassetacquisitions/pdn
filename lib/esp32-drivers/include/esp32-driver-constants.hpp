#pragma once

#include <cstdint>
#include <sdkconfig.h>

// ESP-NOW Wi-Fi channel. Must match across all devices on the same network.
constexpr uint8_t ESPNOW_CHANNEL = 6;

// The fleet runs at 240MHz with 80MHz PSRAM. kconfgen substitutes a default for
// an option that is out of range or unreachable rather than failing, so a flipped
// clock would otherwise read as a normal build.
static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240,
              "the fleet runs at 240MHz; see sdkconfig.defaults");
static_assert(CONFIG_SPIRAM_SPEED == 80,
              "the fleet runs 80MHz PSRAM; see sdkconfig.defaults");
