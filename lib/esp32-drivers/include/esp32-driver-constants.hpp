#pragma once

#include <cstdint>

// ESP-NOW Wi-Fi channel. Must match across all devices on the same network.
constexpr uint8_t ESPNOW_CHANNEL = 6;

// How long the radio is given to settle between a mode change and the channel
// pin. Inherited from the Arduino bring-up, which delayed the same 100ms here.
constexpr uint32_t WIFI_SETTLE_MS = 100;
