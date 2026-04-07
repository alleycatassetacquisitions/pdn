#pragma once

#include "image.hpp"
#include "images-raw.hpp"
#include "device/drivers/light-interface.hpp"

const Image glassesImage = Image(image_glasses, 128, 64, 0, 0);
const Image alleycatLogoImage = Image(image_logo_alley, 128, 64, 32, 0);
const Image largeButtonImage = Image(image_large_button, 128, 64, 0, 0);
const Image smallButtonImage = Image(image_small_button, 128, 64, 0, 0);

const static LEDColor mainMenuFinColors[13] = {
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(0, 0, 0),
    LEDColor(237, 75, 0),
    LEDColor(255, 2, 1),
    LEDColor(255, 2, 1),
    LEDColor(237, 75, 0),
};

const static LEDColor mainMenuRecessColors[23] = {
    LEDColor(0, 255, 0),    // Green
    LEDColor(0, 237, 75),   // Blue-green
    LEDColor(0, 200, 100),  // Teal
    LEDColor(0, 180, 180),  // Cyan
    LEDColor(0, 255, 0),    // Green
    LEDColor(0, 237, 75),   // Blue-green
    LEDColor(0, 200, 100),  // Teal
    LEDColor(0, 180, 180),  // Cyan
    LEDColor(0, 255, 0),    // Green
    LEDColor(0, 237, 75),   // Blue-green
    LEDColor(0, 200, 100),  // Teal
    LEDColor(0, 180, 180),  // Cyan
    LEDColor(0, 255, 0),    // Green
    LEDColor(0, 237, 75),   // Blue-green
    LEDColor(0, 200, 100),  // Teal
    LEDColor(0, 180, 180),  // Cyan
    LEDColor(0, 255, 0),    // Green
    LEDColor(0, 237, 75),   // Blue-green
    LEDColor(0, 200, 100),  // Teal
    LEDColor(0, 180, 180),  // Cyan
    LEDColor(0, 255, 0),    // Green
    LEDColor(0, 237, 75),   // Blue-green
    LEDColor(0, 200, 100),  // Teal
};

const static LEDState menuLightsInitialState = [](){
    LEDState state;
    for(int i = 0; i < 23; i++) {
        state.setRecessLight(i, mainMenuRecessColors[i], 87);
    }
    for(int i = 0; i < 9; i++) {
        state.setFinLight(i, mainMenuFinColors[i], 87);
    }
    return state;
}();