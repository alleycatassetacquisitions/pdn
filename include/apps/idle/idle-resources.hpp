#pragma once

#include "image.hpp"
#include "images-raw.hpp"
#include "device/drivers/light-interface.hpp"

const Image glassesImage = Image(image_glasses, 128, 64, 0, 0);
const Image alleycatLogoImage = Image(image_logo_alley, 128, 64, 32, 0);

const static LEDColor idleFinColors[13] = {
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