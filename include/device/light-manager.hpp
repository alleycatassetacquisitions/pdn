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
#include "light-interface.hpp"
#include "game/quickdraw-resources.hpp"  // For easing curve lookup tables
#include "device/pdn-lights.hpp"
#include "device/idle-animation.hpp"
#include "device/countdown-animation.hpp"
#include "device/vertical-chase-animation.hpp"
#include "device/transmit-breath-animation.hpp"
#include "device/hunter-win-animation.hpp"
#include "device/bounty-win-animation.hpp"
#include "device/lose-animation.hpp"

class LightManager {
public:
    LightManager(PDNLightStrip& displayLights, PDNLightStrip& gripLights);
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
    // void setLightColor(LightIdentifier lights, uint8_t index, LEDColor color);
    // void setAllLights(LightIdentifier lights, LEDColor color);
    // void setBrightness(LightIdentifier lights, uint8_t brightness);
    void clear(LightIdentifier lights);
    
    // Animation state query
    bool isAnimating() const;
    bool isPaused() const;
    bool isAnimationComplete() const;
    AnimationType getCurrentAnimation() const;

private:
    /*
        These methods are used to extract the CRGB arrays from the LEDState.
        This is useful for sending the LEDState to the FastLED library.
        Extract grip lights will create a CRGB array from the LEDState like this:

        gripLight[0] = state.leftLights[0];
        gripLight[1] = state.leftLights[1];
        gripLight[2] = state.leftLights[2];
        gripLight[3] = state.rightLights[2];
        gripLight[4] = state.rightLights[1];
        gripLight[5] = state.rightLights[0];
        
        And extract display lights will create a CRGB array from the LEDState like this:

        displayLight[0] = state.leftLights[3];
        displayLight[1] = state.leftLights[4];
        displayLight[2] = state.leftLights[5];
        displayLight[3] = state.leftLights[6];
        displayLight[4] = state.leftLights[7];
        displayLight[5] = state.leftLights[8];
        displayLight[6] = state.rightLights[8];
        displayLight[7] = state.rightLights[7];
        displayLight[8] = state.rightLights[6];
        displayLight[9] = state.rightLights[5];
        displayLight[10] = state.rightLights[4];
        displayLight[11] = state.rightLights[3];
        displayLight[12] = state.transmitLight;
        
    */
    void mapStateToGripLights(const LEDState& state);
    void mapStateToDisplayLights(const LEDState& state);
    
    /*
        This method will apply the LEDState to the physical LEDs.
        It will use the arrays extracted from the LEDState to set the color and brightness of the LEDs.
        This should be a pair of simple for loops, one that iterates over the grip lights
        and one that iterates over the display lights. For each LED, we will set the color and brightness.
    */ 
    void applyLEDState(const LEDState& state);
    
    // Member variables
    PDNLightStrip& displayLights;
    PDNLightStrip& gripLights;
    IAnimation* currentAnimation;
    
    // Member arrays for extracted lights
    LEDState::SingleLEDState gripLightArray[6];
    LEDState::SingleLEDState displayLightArray[13];
};