#pragma once

#include "device/drivers/esp32-s3/u8g2-display-driver.hpp"

/**
 * The FDN's panel.
 *
 * NONAME0 vs NONAME2: u8g2's SSD1309 128x64 "NONAME2" build adds a +2 column address offset when
 * blitting to the panel (u8x8: default_x_offset=2) for 132-px-wide internal RAM. Use NONAME0 when
 * the FDN panel maps buffer column 0 to the left edge; NONAME2 otherwise—wrong choice shows a
 * ghost/fixed column and wrong half-screen split.
 */
class SSD1309U8G2Driver : public U8g2DisplayDriver {
public:
    /** Pins are chip-select, data/command and reset; clock and data are fixed. */
    explicit SSD1309U8G2Driver(const std::string& name, uint8_t csPin, uint8_t dcPin, uint8_t rstPin)
        : U8g2DisplayDriver(name, u8g2_Setup_ssd1309_128x64_noname0_f, csPin, dcPin, rstPin) {}
};
