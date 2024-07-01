#pragma once

#include "base-state.hpp"

class Example1State : public BaseDeviceState {
    public:
        int Update();
        int Render(U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display,
                   CRGB* display_lights, 
                   size_t num_display_lights,
                   CRGB* grip_lights,
                   size_t num_grip_lights);

        DeviceState GetState();

    private:
        int m_numTimesUpdated = 0;
};
