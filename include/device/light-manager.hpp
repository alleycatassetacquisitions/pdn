/*
    This class is responsible for managing the LED's on the device.

    It will be responsible for:
    - Keeping track of the state of LED's,
    - loading and unloading animations and color palettes.
    - setting brightness - brightness values should always be clamped between 0 and 255.
    - any other led management functions.

    This class relies on the FastLED library.

    When loading an animation, we should be able to load a color palette and a bezier curve.
    The bezier should be optional, and if no bezier is provided, a linear animation should be used.
*/

#pragma once

#include <FastLED.h>
#include "simple-timer.hpp"
#include "light-interface.hpp"
#include "game/quickdraw-resources.hpp"  // For easing curve lookup tables
#include "device/display-lights.hpp"
#include "device/grip-lights.hpp"
#include "device/idle-animation.hpp"
#include "device/countdown-animation.hpp"

class LightManager {
public:
    LightManager(DisplayLights& displayLights, GripLights& gripLights);
    ~LightManager();

    // Core functionality
    void begin();
    void loop();
    
    // Animation control
    void startAnimation(AnimationConfig config);
    void stopAnimation();
    void pauseAnimation();
    void resumeAnimation();
    
    // Direct LED control
    void setLightColor(LightIdentifier lights, uint8_t index, LEDColor color);
    void setAllLights(LightIdentifier lights, LEDColor color);
    void setBrightness(LightIdentifier lights, uint8_t brightness);
    void clear(LightIdentifier lights);
    
    // Color palette management
    void setPalette(const LEDColor* colors, uint8_t numColors);
    LEDColor getPaletteColor(uint8_t index) const;
    
    // Animation state query
    bool isAnimating() const;
    bool isPaused() const;
    bool isAnimationComplete() const;
    AnimationType getCurrentAnimation() const;

protected:
    // Helper methods
    uint8_t getEasingValue(uint8_t progress, EaseCurve curve) const;
    LEDColor interpolateColor(const LEDColor& start, const LEDColor& end, uint8_t t) const;

private:
    // FastLED helpers
    static CRGB convertToFastLED(const LEDColor& color);
    static LEDColor convertFromFastLED(const CRGB& color);
    
    // Apply an LED state to the physical LEDs
    void applyLEDState(const LEDState& state);
    
    // Member variables
    DisplayLights& displayLights;
    GripLights& gripLights;
    IAnimation* currentAnimation;
    LEDColor* palette;
    uint8_t paletteSize;
};