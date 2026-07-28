#pragma once

#include <string>

// Serial communication speed shared by all serial drivers.
// uint32_t, not uint16_t: common rates above 65535 (115200 among them) wrap in 16
// bits and would silently configure the UART for a different speed.
constexpr uint32_t BAUDRATE = 19200;
static_assert(static_cast<decltype(BAUDRATE)>(115200) == 115200,
              "BAUDRATE type cannot express 115200; in 16 bits it wraps to 49664");

// Delimiters for SerialManager's string channel.
constexpr char STRING_TERM = '\r';
constexpr char STRING_START = '*';
constexpr uint16_t TRANSMIT_QUEUE_MAX_SIZE = 1024;
