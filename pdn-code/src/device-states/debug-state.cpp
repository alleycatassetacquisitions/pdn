#include "../../include/device-states/debug-state.hpp"

int DebugState::Update() {
    return 0;
}

int DebugState::Render(U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display, CRGB *display_lights, size_t num_display_lights, CRGB *grip_lights, size_t num_grip_lights)
{
    display.print("DEBUG: " + debugModeCurrentOperation);
    display.setCursor(0, 16);
    display.print(u8x8_u8toa(screenCounter++, 3));
    return 0;
}

DeviceState DebugState::GetState()
{
    return DeviceState::debug;
}
