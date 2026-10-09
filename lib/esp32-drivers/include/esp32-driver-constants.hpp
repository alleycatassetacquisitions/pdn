#pragma once

#include <cstdint>
#include <sdkconfig.h>

// ESP-NOW Wi-Fi channel. Must match across all devices on the same network.
constexpr uint8_t ESPNOW_CHANNEL = 6;

// kconfgen substitutes a default for an option that is out of range or
// unreachable rather than failing, so a flipped clock reads as a normal build.
// The retransmit spans, the serial jack drain and the duel-press sampling latency
// were tuned on devices running at these two speeds.
static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240,
              "timing constants were tuned at 240MHz");
static_assert(CONFIG_SPIRAM_SPEED == 80,
              "the ESP-NOW packet buffers were tuned against 80MHz PSRAM");
