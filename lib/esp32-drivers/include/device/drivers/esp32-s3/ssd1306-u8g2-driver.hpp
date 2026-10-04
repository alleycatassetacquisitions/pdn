#pragma once

#include "device/drivers/esp32-s3/u8g2-display-driver.hpp"

/** The PDN's panel. */
class SSD1306U8G2Driver : public U8g2DisplayDriver {
public:
    /** Pins are chip-select, data/command and reset; clock and data are fixed. */
    explicit SSD1306U8G2Driver(const std::string& name, uint8_t csPin, uint8_t dcPin, uint8_t rstPin)
        : U8g2DisplayDriver(name, u8g2_Setup_ssd1306_128x64_noname_f, csPin, dcPin, rstPin) {}
};
