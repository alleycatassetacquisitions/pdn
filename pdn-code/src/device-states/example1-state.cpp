
#include "../../include/device-states/example1-state.hpp"

int Example1State::Update() {
    m_numTimesUpdated++;
    return 0;
}

int Example1State::Render(U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display, CRGB *display_lights, size_t num_display_lights, CRGB *grip_lights, size_t num_grip_lights)
{
    for(size_t i = 0; i < num_display_lights; ++i)
    {
        display_lights[i] = CRGB::HTMLColorCode::Blue;
    }
    return 0;
}

DeviceState Example1State::GetState()
{
    return DeviceState::kExample1;
}
