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


void LightManager::clear(LightIdentifier lights) {
    // Clear grip lights
    for (int i = 0; i < 6; i++) {
        gripLightArray[i] = LEDPhysicalState();  // This will set color to CRGB::Black and reserved to false
    }
    
    // Clear display lights
    for (int i = 0; i < 13; i++) {
        displayLightArray[i] = LEDPhysicalState();  // This will set color to CRGB::Black and reserved to false
    }
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
    gripLightArray[0] = LEDPhysicalState(convertToFastLED(state.leftLights[2]), state.leftLights[2].reserved);
    gripLightArray[1] = LEDPhysicalState(convertToFastLED(state.leftLights[1]), state.leftLights[1].reserved);
    gripLightArray[2] = LEDPhysicalState(convertToFastLED(state.leftLights[0]), state.leftLights[0].reserved);
    gripLightArray[3] = LEDPhysicalState(convertToFastLED(state.rightLights[0]), state.rightLights[0].reserved);
    gripLightArray[4] = LEDPhysicalState(convertToFastLED(state.rightLights[1]), state.rightLights[1].reserved);
    gripLightArray[5] = LEDPhysicalState(convertToFastLED(state.rightLights[2]), state.rightLights[2].reserved);
}

void LightManager::mapStateToDisplayLights(const LEDState& state) {
    // Map the LEDState to the display lights array according to the pattern
    displayLightArray[0] = LEDPhysicalState(convertToFastLED(state.leftLights[3]), state.leftLights[3].reserved);
    displayLightArray[1] = LEDPhysicalState(convertToFastLED(state.leftLights[4]), state.leftLights[4].reserved);
    displayLightArray[2] = LEDPhysicalState(convertToFastLED(state.leftLights[5]), state.leftLights[5].reserved);
    displayLightArray[3] = LEDPhysicalState(convertToFastLED(state.leftLights[6]), state.leftLights[6].reserved);
    displayLightArray[4] = LEDPhysicalState(convertToFastLED(state.leftLights[7]), state.leftLights[7].reserved);
    displayLightArray[5] = LEDPhysicalState(convertToFastLED(state.leftLights[8]), state.leftLights[8].reserved);
    displayLightArray[6] = LEDPhysicalState(convertToFastLED(state.rightLights[8]), state.rightLights[8].reserved);
    displayLightArray[7] = LEDPhysicalState(convertToFastLED(state.rightLights[7]), state.rightLights[7].reserved);
    displayLightArray[8] = LEDPhysicalState(convertToFastLED(state.rightLights[6]), state.rightLights[6].reserved);
    displayLightArray[9] = LEDPhysicalState(convertToFastLED(state.rightLights[5]), state.rightLights[5].reserved);
    displayLightArray[10] = LEDPhysicalState(convertToFastLED(state.rightLights[4]), state.rightLights[4].reserved);
    displayLightArray[11] = LEDPhysicalState(convertToFastLED(state.rightLights[3]), state.rightLights[3].reserved);
    displayLightArray[12] = LEDPhysicalState(convertToFastLED(state.transmitLight), state.transmitLight.reserved);
}

void LightManager::applyLEDState(const LEDState& state) {
    // Map the LEDState to the physical LEDs, respecting reserved status
    mapStateToGripLights(state);
    mapStateToDisplayLights(state);
    
    // Apply the grip lights
    for (int i = 0; i < 6; i++) {
        if (!gripLightArray[i].reserved) {
            this->gripLights.setLight(i, gripLightArray[i].color);
        }
    }
    
    for (int i = 0; i < 13; i++) {
        if (!displayLightArray[i].reserved) {
            this->displayLights.setLight(i, displayLightArray[i].color);
        }
    }
}

void LightManager::setLED(LightIdentifier whichLights, int ledNum, LEDColor color, uint8_t brightness, bool reserved) {
    // Create a new LEDState with the specified parameters
    LEDState state;
    
    // Map the LED number to the correct index based on the identifier
    switch (whichLights) {
        case LightIdentifier::LEFT_LIGHTS:
            if (ledNum < 9) {
                state.leftLights[ledNum] = LEDState::SingleLEDState(color, brightness, reserved);
            }
            break;
        case LightIdentifier::RIGHT_LIGHTS:
            if (ledNum < 9) {
                state.rightLights[ledNum] = LEDState::SingleLEDState(color, brightness, reserved);
            }
            break;
        case LightIdentifier::DISPLAY_LIGHTS:
            // Map display LED indices to the correct left/right light indices
            if (ledNum < 6) {
                // Left side display LEDs (0-5)
                state.leftLights[ledNum + 3] = LEDState::SingleLEDState(color, brightness, reserved);
            } else if (ledNum < 12) {
                // Right side display LEDs (6-11)
                state.rightLights[11 - ledNum] = LEDState::SingleLEDState(color, brightness, reserved);
            } else if (ledNum == 12) {
                // Transmit LED
                state.transmitLight = LEDState::SingleLEDState(color, brightness, reserved);
            }
            break;
        case LightIdentifier::GRIP_LIGHTS:
            // Map grip LED indices to the correct left/right light indices
            if (ledNum < 3) {
                // Left grip LEDs (0-2)
                state.leftLights[2 - ledNum] = LEDState::SingleLEDState(color, brightness, reserved);
            } else if (ledNum < 6) {
                // Right grip LEDs (3-5)
                state.rightLights[ledNum - 3] = LEDState::SingleLEDState(color, brightness, reserved);
            }
            break;
        case LightIdentifier::TRANSMIT_LIGHT:
            state.transmitLight = LEDState::SingleLEDState(color, brightness, reserved);
            break;
        default:
            break;
    }
} 