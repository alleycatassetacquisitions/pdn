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

class LightManager : public ILightController {
public:
    LightManager(DisplayLights& displayLights, GripLights& gripLights);
    ~LightManager() override;

    // ILightController implementation
    void begin() override;
    void loop() override;
    
    void startAnimation(AnimationConfig config) override;
    void stopAnimation() override;
    void pauseAnimation() override;
    void resumeAnimation() override;
    
    void setLightColor(LightIdentifier lights, uint8_t index, LEDColor color) override;
    void setAllLights(LightIdentifier lights, LEDColor color) override;
    void setBrightness(LightIdentifier lights, uint8_t brightness) override;
    void clear(LightIdentifier lights) override;
    
    void setPalette(const LEDColor* colors, uint8_t numColors) override;
    LEDColor getPaletteColor(uint8_t index) const override;
    
    bool isAnimating() const override;
    bool isPaused() const override;
    AnimationType getCurrentAnimation() const override;

protected:
    uint8_t getEasingValue(uint8_t progress, EaseCurve curve) const override;
    LEDColor interpolateColor(const LEDColor& start, const LEDColor& end, uint8_t t) const override;

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