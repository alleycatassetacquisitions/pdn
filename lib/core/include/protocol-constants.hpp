#pragma once

#include <string>

// Serial communication speed shared by all serial drivers and the handshake protocol.
// uint32_t, not uint16_t: common rates above 65535 (115200 among them) wrap in 16
// bits and would silently configure the UART for a different speed.
constexpr uint32_t BAUDRATE = 19200;
static_assert(static_cast<decltype(BAUDRATE)>(115200) == 115200,
              "BAUDRATE type cannot express 115200; in 16 bits it wraps to 49664");

// Heartbeat token sent on the serial line to keep connections alive.
inline const std::string SERIAL_HEARTBEAT = "hb";

// Serial framing protocol constants used by SerialManager, RDC, and handshake.
inline const std::string SEND_MAC_ADDRESS = "smac";
constexpr char PORT_SEPARATOR = '#';
constexpr char DEVICE_TYPE_SEPARATOR = 't';
constexpr char STRING_TERM = '\r';
constexpr char STRING_START = '*';
constexpr uint16_t TRANSMIT_QUEUE_MAX_SIZE = 1024;
