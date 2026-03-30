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

#include "utils/simple-timer.hpp"
#include "drivers/light-interface.hpp"

class LightManager {
public:
    LightManager(LightStrip& pdnLights);
    ~LightManager();

    // Core functionality
    void loop();
    
    // Animation control
    void startAnimation(AnimationConfig config);
    void stopAnimation();
    void pauseAnimation();
    void resumeAnimation();

    void setGlobalBrightness(uint8_t brightness);
    
    void clear();
    
    // Animation state query
    bool isAnimating() const;
    bool isPaused() const;
    bool isAnimationComplete() const;
    AnimationType getCurrentAnimation() const;

private:
    void mapStateToRecessLights(const LEDState& state);
    void mapStateToFinLights(const LEDState& state);
    void applyLEDState(const LEDState& state);
    
    LightStrip& lights;
    IAnimation* currentAnimation;
    
    LEDState::SingleLEDState recessLightArray[23];
    LEDState::SingleLEDState finLightArray[9];
};