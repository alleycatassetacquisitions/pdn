#pragma once

#include <string>

// Serial communication speed shared by all serial drivers and the handshake protocol.
constexpr uint16_t BAUDRATE = 19200;

// Heartbeat token sent on the serial line to keep connections alive.
inline const std::string SERIAL_HEARTBEAT = "hb";

// Serial framing protocol constants used by SerialManager, RDC, and handshake.
inline const std::string SEND_MAC_ADDRESS = "smac";
constexpr char PORT_SEPARATOR = '#';
constexpr char DEVICE_TYPE_SEPARATOR = 't';
constexpr char STRING_TERM = '\r';
constexpr char STRING_START = '*';
constexpr uint16_t TRANSMIT_QUEUE_MAX_SIZE = 1024;
