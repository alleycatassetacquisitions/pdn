#pragma once
#include <cstdint>
#include <cstddef>

// CRC-16/CCITT XMODEM: poly 0x1021, init 0x0000, no reflection, no xorout.
// The UART has no frame check of its own; ESP-NOW integrity is the radio's
// hardware FCS.

// Fold one more span into a running CRC. Lets a caller checksum several
// non-contiguous spans (e.g. opcode then payload) without concatenating them.
inline uint16_t crc16Update(uint16_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

inline uint16_t crc16(const uint8_t* data, size_t len) {
    return crc16Update(0x0000, data, len);
}
