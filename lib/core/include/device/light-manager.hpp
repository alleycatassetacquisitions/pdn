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
#include "device/animation/animation-base.hpp"

class LightManager {
public:
    LightManager(LightStrip& pdnLights);
    virtual ~LightManager();

    // Core functionality
    void loop();
    
    // Animation control
    void startAnimation(AnimationBase* animation, AnimationConfig config);
    void setAnimationSpeed(uint8_t speedMs);
    void stopAnimation();
    void pauseAnimation();
    void resumeAnimation();

    void setGlobalBrightness(uint8_t brightness);
    
    void clear();
    
    // Animation state query
    bool isAnimating() const;
    bool isPaused() const;
    bool isAnimationComplete() const;

protected:
    // Override in subclasses to provide a device-specific LED mapping.
    // The default implementation uses PDN layout (6 grip + 13 display).
    virtual void applyLEDState(const LEDState& state);

    LightStrip& pdnLights;

private:
    /*
        PDN LED mapping:
        gripLight[0-2]   = leftLights[0-2]
        gripLight[3-5]   = rightLights[0-2]
        displayLight[0-5]  = leftLights[3-8]
        displayLight[6-11] = rightLights[3-8] (reversed)
        displayLight[12]   = transmitLight
    */
    void mapStateToGripLights(const LEDState& state);
    void mapStateToDisplayLights(const LEDState& state);

    IAnimation* currentAnimation;

    // Member arrays for extracted lights (PDN layout: 6 grip + 13 display)
    LEDState::SingleLEDState gripLightArray[6];
    LEDState::SingleLEDState displayLightArray[13];
};