#pragma once

#include <U8g2lib.h>
#include <FastLED.h>

/* 
Base of polymorphic state machine for tracking device state

This is an abstract class that just creates a common interface for our states.

To use, just declare a pointer to base state and point to an instance of one of the
derived state classes. Then periodically call Update() and Render() on the pointer.
To change states, change instance that base pointer points to. See main.cpp for example usage.
*/


//List of all device state classes. This will be used for querying what type
//a state actually is, sometimes useful for control flow or upcasting
enum class DeviceState {
    kExample1, //TODO: Remove once we have a few real states
    kNumDeviceStates //NOTE: Not a real state
};

class BaseDeviceState {
public:
    //Update state data and run control logic. Return 0 for success or error value
    //for failure.
    virtual int Update() = 0;

    //Draw current state to display and set lights. Return 0 for success or error
    //value for failure.
    virtual int Render(U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display,
                    CRGB* display_lights, 
                    size_t num_display_lights,
                    CRGB* grip_lights,
                    size_t num_grip_lights) = 0;

    //Query what kind of state this is
    virtual DeviceState GetState() = 0;
};
