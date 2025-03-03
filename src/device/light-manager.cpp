#include "device/light-manager.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"

static const char* TAG = "LightManager";

LightManager::LightManager(DisplayLights& displayLights, GripLights& gripLights)
    : displayLights(displayLights)
    , gripLights(gripLights)
    , currentAnimation(nullptr) {
}

LightManager::~LightManager() {
    if (currentAnimation) {
        delete currentAnimation;
    }
}

void LightManager::begin() {
    // Nothing to initialize
}

void LightManager::loop() {
    if (currentAnimation && !currentAnimation->isPaused()) {
        // Get the next frame from the animation
        LEDState state = currentAnimation->animate();
        
        // Apply the state to the physical LEDs
        applyLEDState(state);
    }
    
    // Update the physical LEDs
    FastLED.show();
}

void LightManager::startAnimation(AnimationConfig config) {
    // Clean up any existing animation
    if (currentAnimation) {
        delete currentAnimation;
        currentAnimation = nullptr;
    }
    
    // Create the appropriate animation based on type
    switch (config.type) {
        case AnimationType::IDLE:
            currentAnimation = new IdleAnimation();
            break;
        case AnimationType::VERTICAL_CHASE:
            currentAnimation = new VerticalChaseAnimation();
            break;
        case AnimationType::DEVICE_CONNECTED:
            // TODO: Implement other animation types
            break;
        case AnimationType::COUNTDOWN:
            // currentAnimation = new CountdownAnimation();
            break;
        case AnimationType::LOSE:
            break;
        case AnimationType::WIN:
            break;
    }
    
    // Initialize the animation if created
    if (currentAnimation) {
        // Initialize the animation with the config (which now includes initialState)
        currentAnimation->init(config);
    }
}

void LightManager::stopAnimation() {
    if (currentAnimation) {
        currentAnimation->stop();
        delete currentAnimation;
        currentAnimation = nullptr;
    }
    clear(LightIdentifier::DISPLAY_LIGHTS);
    clear(LightIdentifier::GRIP_LIGHTS);
}

void LightManager::pauseAnimation() {
    if (currentAnimation) {
        currentAnimation->pause();
    }
}

void LightManager::resumeAnimation() {
    if (currentAnimation) {
        currentAnimation->resume();
    }
}

// void LightManager::setLightColor(LightIdentifier lights, uint8_t index, LEDColor color) {
//     CRGB fastledColor = convertToFastLED(color);
//     switch (lights) {
//         case LightIdentifier::DISPLAY_LIGHTS:
//             displayLights.setLight(index, fastledColor);
//             break;
//         case LightIdentifier::GRIP_LIGHTS:
//             gripLights.setLight(index, fastledColor);
//             break;
//         case LightIdentifier::LEFT_LIGHTS:
//             switch(index) {
//                 case 0:
//                     gripLights.setLight(2, fastledColor);
//                     break;
//                 case 1:
//                     gripLights.setLight(1, fastledColor);
//                     break;
//                 case 2:
//                     gripLights.setLight(0, fastledColor);
//                     break;
//                 case 3:
//                     displayLights.setLight(0, fastledColor);
//                     break;
//                 case 4:
//                     displayLights.setLight(1, fastledColor);
//                     break;
//                 case 5:
//                     displayLights.setLight(2, fastledColor);
//                     break;
//                 case 6:
//                     displayLights.setLight(3, fastledColor);
//                     break;
//                 case 7:
//                     displayLights.setLight(4, fastledColor);
//                     break;
//                 case 8:
//                     displayLights.setLight(5, fastledColor);
//                     break;
//             }
//             break;
//         case LightIdentifier::RIGHT_LIGHTS:
//             switch(index) {
//                 case 0:
//                     gripLights.setLight(3, fastledColor);
//                     break;
//                 case 1:
//                     gripLights.setLight(4, fastledColor);
//                     break;
//                 case 2:
//                     gripLights.setLight(5, fastledColor);
//                     break;
//                 case 3:
//                     displayLights.setLight(11, fastledColor);
//                     break;
//                 case 4:
//                     displayLights.setLight(10, fastledColor);
//                     break;
//                 case 5:
//                     displayLights.setLight(9, fastledColor);
//                     break;
//                 case 6:
//                     displayLights.setLight(8, fastledColor);
//                     break;
//                 case 7:
//                     displayLights.setLight(7, fastledColor);
//                     break;
//                 case 8:
//                     displayLights.setLight(6, fastledColor);
//                     break;
//             }
//             break;
//         case LightIdentifier::TRANSMIT_LIGHT:
//             displayLights.setLight(12, fastledColor);
//             break;
//         default:
//             break;
//     }
// }

// void LightManager::setAllLights(LightIdentifier lights, LEDColor color) {
//     CRGB fastledColor = convertToFastLED(color);
//     switch (lights) {
//         case LightIdentifier::DISPLAY_LIGHTS:
//             for (int i = 0; i < 13; i++) {
//                 displayLights.setLight(i, fastledColor);
//             }
//             break;
//         case LightIdentifier::GRIP_LIGHTS:
//             for (int i = 0; i < 6; i++) {
//                 gripLights.setLight(i, fastledColor);
//             }
//             break;
//         case LightIdentifier::LEFT_LIGHTS:
//             for (int i = 6; i < 12; i++) {
//                 displayLights.setLight(i, fastledColor);
//             }
//             break;
//         case LightIdentifier::RIGHT_LIGHTS:
//             for (int i = 0; i < 6; i++) {
//                 displayLights.setLight(i, fastledColor);
//             }
//             break;
//         case LightIdentifier::TRANSMIT_LIGHT:
//             displayLights.setLight(12, fastledColor);
//             break;
//         default:
//             break;
//     }
// }

// void LightManager::setBrightness(LightIdentifier lights, uint8_t brightness) {
//     switch (lights) {
//         case LightIdentifier::DISPLAY_LIGHTS:
//             for (int i = 0; i < 13; i++) {
//                 displayLights.setLightBrightness(i, brightness);
//             }
//             break;
//         case LightIdentifier::GRIP_LIGHTS:
//             for (int i = 0; i < 6; i++) {
//                 gripLights.setLightBrightness(i, brightness);
//             }
//             break;
//         case LightIdentifier::LEFT_LIGHTS:
//             for (int i = 6; i < 12; i++) {
//                 displayLights.setLightBrightness(i, brightness);
//             }
//             break;
//         case LightIdentifier::RIGHT_LIGHTS:
//             for (int i = 0; i < 6; i++) {
//                 displayLights.setLightBrightness(i, brightness);
//             }
//             break;
//         case LightIdentifier::TRANSMIT_LIGHT:
//             displayLights.setLightBrightness(12, brightness);
//             break;
//         default:
//             break;
//     }
// }

void LightManager::clear(LightIdentifier lights) {
    fill_solid(gripLightArray, 6, CRGB::Black);
    fill_solid(displayLightArray, 13, CRGB::Black);
}

bool LightManager::isAnimating() const {
    return currentAnimation != nullptr && !currentAnimation->isComplete();
}

bool LightManager::isPaused() const {
    return currentAnimation && currentAnimation->isPaused();
}

bool LightManager::isAnimationComplete() const {
    return currentAnimation ? currentAnimation->isComplete() : true;
}

AnimationType LightManager::getCurrentAnimation() const {
    return currentAnimation ? currentAnimation->getType() : AnimationType::IDLE;
}

// Private methods
CRGB LightManager::convertToFastLED(const LEDState::SingleLEDState& state) {
    CRGB fastledColor = CRGB(state.color.red, state.color.green, state.color.blue);
    fastledColor.nscale8_video(state.brightness);
    return fastledColor;
}

LEDColor LightManager::convertFromFastLED(const CRGB& color) {
    return LEDColor(color.r, color.g, color.b);
}

void LightManager::mapStateToGripLights(const LEDState& state) {
    // Map the LEDState to the grip lights array according to the pattern
    gripLightArray[0] = convertToFastLED(state.leftLights[2]);
    gripLightArray[1] = convertToFastLED(state.leftLights[1]);
    gripLightArray[2] = convertToFastLED(state.leftLights[0]);
    gripLightArray[3] = convertToFastLED(state.rightLights[0]);
    gripLightArray[4] = convertToFastLED(state.rightLights[1]);
    gripLightArray[5] = convertToFastLED(state.rightLights[2]);
}

void LightManager::mapStateToDisplayLights(const LEDState& state) {
    // Map the LEDState to the display lights array according to the pattern
    displayLightArray[0] = convertToFastLED(state.leftLights[3]);
    displayLightArray[1] = convertToFastLED(state.leftLights[4]);
    displayLightArray[2] = convertToFastLED(state.leftLights[5]);
    displayLightArray[3] = convertToFastLED(state.leftLights[6]);
    displayLightArray[4] = convertToFastLED(state.leftLights[7]);
    displayLightArray[5] = convertToFastLED(state.leftLights[8]);
    displayLightArray[6] = convertToFastLED(state.rightLights[8]);
    displayLightArray[7] = convertToFastLED(state.rightLights[7]);
    displayLightArray[8] = convertToFastLED(state.rightLights[6]);
    displayLightArray[9] = convertToFastLED(state.rightLights[5]);
    displayLightArray[10] = convertToFastLED(state.rightLights[4]);
    displayLightArray[11] = convertToFastLED(state.rightLights[3]);
    displayLightArray[12] = convertToFastLED(state.transmitLight);
}

void LightManager::applyLEDState(const LEDState& state) {
    // Extract the grip and display lights from the LEDState
    mapStateToGripLights(state);
    mapStateToDisplayLights(state);
    
    // Apply the grip lights
    for (int i = 0; i < 6; i++) {
        // Set the color
        this->gripLights.setLight(i, gripLightArray[i]);
    }
    
    // Apply the display lights
    for (int i = 0; i < 13; i++) {
        this->displayLights.setLight(i, displayLightArray[i]);
    }
} 