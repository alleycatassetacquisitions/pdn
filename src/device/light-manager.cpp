#include "device/light-manager.hpp"

LightManager::LightManager(DisplayLights& displayLights, GripLights& gripLights)
    : displayLights(displayLights)
    , gripLights(gripLights)
    , currentAnimation(nullptr)
    , palette(nullptr)
    , paletteSize(0) {
}

LightManager::~LightManager() {
    if (currentAnimation) {
        delete currentAnimation;
    }
    if (palette) {
        delete[] palette;
    }
}

void LightManager::begin() {
    // Nothing to initialize
}

void LightManager::loop() {
    if (currentAnimation && !currentAnimation->isPaused()) {
        // Get the next frame from the animation
        LEDState state = currentAnimation->animate(millis());
        
        // Apply the state to the physical LEDs
        applyLEDState(state);
    }
    
    // Allow the light strips to update their state
    displayLights.loop();
    gripLights.loop();
    
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
        case AnimationType::DEVICE_CONNECTED:
            // TODO: Implement other animation types
            break;
        case AnimationType::COUNTDOWN:
            currentAnimation = new CountdownAnimation();
            break;
        case AnimationType::LOSE:
            break;
        case AnimationType::WIN:
            break;
    }
    
    // Initialize the animation if created
    if (currentAnimation) {
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

void LightManager::setLightColor(LightIdentifier lights, uint8_t index, LEDColor color) {
    CRGB fastledColor = convertToFastLED(color);
    switch (lights) {
        case LightIdentifier::DISPLAY_LIGHTS:
            displayLights.setLight(index, fastledColor);
            break;
        case LightIdentifier::GRIP_LIGHTS:
            gripLights.setLight(index, fastledColor);
            break;
        case LightIdentifier::LEFT_LIGHTS:
            switch(index) {
                case 0:
                    gripLights.setLight(2, fastledColor);
                    break;
                case 1:
                    gripLights.setLight(1, fastledColor);
                    break;
                case 2:
                    gripLights.setLight(0, fastledColor);
                    break;
                case 3:
                    displayLights.setLight(0, fastledColor);
                    break;
                case 4:
                    displayLights.setLight(1, fastledColor);
                    break;
                case 5:
                    displayLights.setLight(2, fastledColor);
                    break;
                case 6:
                    displayLights.setLight(3, fastledColor);
                    break;
                case 7:
                    displayLights.setLight(4, fastledColor);
                    break;
                case 8:
                    displayLights.setLight(5, fastledColor);
                    break;
            }
            break;
        case LightIdentifier::RIGHT_LIGHTS:
            switch(index) {
                case 0:
                    gripLights.setLight(3, fastledColor);
                    break;
                case 1:
                    gripLights.setLight(4, fastledColor);
                    break;
                case 2:
                    gripLights.setLight(5, fastledColor);
                    break;
                case 3:
                    displayLights.setLight(11, fastledColor);
                    break;
                case 4:
                    displayLights.setLight(10, fastledColor);
                    break;
                case 5:
                    displayLights.setLight(9, fastledColor);
                    break;
                case 6:
                    displayLights.setLight(8, fastledColor);
                    break;
                case 7:
                    displayLights.setLight(7, fastledColor);
                    break;
                case 8:
                    displayLights.setLight(6, fastledColor);
                    break;
            }
            break;
        case LightIdentifier::TRANSMIT_LIGHT:
            displayLights.setTransmitLight(fastledColor);
            break;
        default:
            break;
    }
}

void LightManager::setAllLights(LightIdentifier lights, LEDColor color) {
    CRGB fastledColor = convertToFastLED(color);
    switch (lights) {
        case LightIdentifier::DISPLAY_LIGHTS:
            for (int i = 0; i < 13; i++) {
                displayLights.setLight(i, fastledColor);
            }
            break;
        case LightIdentifier::GRIP_LIGHTS:
            for (int i = 0; i < 6; i++) {
                gripLights.setLight(i, fastledColor);
            }
            break;
        case LightIdentifier::LEFT_LIGHTS:
            for (int i = 6; i < 12; i++) {
                displayLights.setLight(i, fastledColor);
            }
            break;
        case LightIdentifier::RIGHT_LIGHTS:
            for (int i = 0; i < 6; i++) {
                displayLights.setLight(i, fastledColor);
            }
            break;
        case LightIdentifier::TRANSMIT_LIGHT:
            displayLights.setTransmitLight(fastledColor);
            break;
        default:
            break;
    }
}

void LightManager::setBrightness(LightIdentifier lights, uint8_t brightness) {
    switch (lights) {
        case LightIdentifier::DISPLAY_LIGHTS:
            for (int i = 0; i < 13; i++) {
                displayLights.setLightBrightness(i, brightness);
            }
            break;
        case LightIdentifier::GRIP_LIGHTS:
            for (int i = 0; i < 6; i++) {
                gripLights.setLightBrightness(i, brightness);
            }
            break;
        case LightIdentifier::LEFT_LIGHTS:
            for (int i = 6; i < 12; i++) {
                displayLights.setLightBrightness(i, brightness);
            }
            break;
        case LightIdentifier::RIGHT_LIGHTS:
            for (int i = 0; i < 6; i++) {
                displayLights.setLightBrightness(i, brightness);
            }
            break;
        case LightIdentifier::TRANSMIT_LIGHT:
            displayLights.setLightBrightness(12, brightness);
            break;
        default:
            break;
    }
}

void LightManager::clear(LightIdentifier lights) {
    setAllLights(lights, LEDColor(0, 0, 0));
}

void LightManager::setPalette(const LEDColor* colors, uint8_t numColors) {
    if (palette) {
        delete[] palette;
        palette = nullptr;
        paletteSize = 0;
    }
    if (colors && numColors > 0) {
        palette = new LEDColor[numColors];
        paletteSize = numColors;
        for (uint8_t i = 0; i < numColors; i++) {
            palette[i] = colors[i];
        }
    }
}

LEDColor LightManager::getPaletteColor(uint8_t index) const {
    if (palette && index < paletteSize) {
        return palette[index];
    }
    return LEDColor(0, 0, 0);
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

uint8_t LightManager::getEasingValue(uint8_t progress, EaseCurve curve) const {
    // Ensure progress is in bounds
    progress = min(progress, (uint8_t)255);
    
    // Return the appropriate easing value from lookup tables
    switch (curve) {
        case EaseCurve::LINEAR:
            return LINEAR_CURVE[progress];
        case EaseCurve::EASE_IN_OUT:
            return EASE_IN_OUT_CURVE[progress];
        case EaseCurve::EASE_OUT:
            return EASE_OUT_CURVE[progress];
        case EaseCurve::ELASTIC:
            return ELASTIC_CURVE[progress];
        default:
            return LINEAR_CURVE[progress];
    }
}

LEDColor LightManager::interpolateColor(const LEDColor& start, const LEDColor& end, uint8_t t) const {
    // Use fixed-point math for interpolation (t is 0-255)
    return LEDColor(
        start.red + ((end.red - start.red) * t) / 255,
        start.green + ((end.green - start.green) * t) / 255,
        start.blue + ((end.blue - start.blue) * t) / 255
    );
}

// Private methods
CRGB LightManager::convertToFastLED(const LEDColor& color) {
    return CRGB(color.red, color.green, color.blue);
}

LEDColor LightManager::convertFromFastLED(const CRGB& color) {
    return LEDColor(color.r, color.g, color.b);
}

void LightManager::applyLEDState(const LEDState& state) {
    // First clear all lights to ensure no residual state
    clear(LightIdentifier::DISPLAY_LIGHTS);
    clear(LightIdentifier::GRIP_LIGHTS);
    
    // Apply left lights (0-8)
    for (int i = 0; i < 9; i++) {
        const auto& led = state.leftLights[i];
        if (led.brightness > 0) {  // Only set if LED is actually on
            // Apply color at full intensity
            setLightColor(LightIdentifier::LEFT_LIGHTS, i, led.color);
            
            // Then apply brightness separately
            // This preserves the color while adjusting intensity
            if (led.brightness < 255) {
                // Get the physical LED indices for this virtual index
                switch(i) {
                    case 0: // Bottom grip LED
                        gripLights.setLightBrightness(2, led.brightness);
                        break;
                    case 1:
                        gripLights.setLightBrightness(1, led.brightness);
                        break;
                    case 2:
                        gripLights.setLightBrightness(0, led.brightness);
                        break;
                    case 3: // Display LEDs
                        displayLights.setLightBrightness(0, led.brightness);
                        break;
                    case 4:
                        displayLights.setLightBrightness(1, led.brightness);
                        break;
                    case 5:
                        displayLights.setLightBrightness(2, led.brightness);
                        break;
                    case 6:
                        displayLights.setLightBrightness(3, led.brightness);
                        break;
                    case 7:
                        displayLights.setLightBrightness(4, led.brightness);
                        break;
                    case 8:
                        displayLights.setLightBrightness(5, led.brightness);
                        break;
                }
            }
        }
    }
    
    // Apply right lights (0-8)
    for (int i = 0; i < 9; i++) {
        const auto& led = state.rightLights[i];
        if (led.brightness > 0) {  // Only set if LED is actually on
            // Apply color at full intensity
            setLightColor(LightIdentifier::RIGHT_LIGHTS, i, led.color);
            
            // Then apply brightness separately
            // This preserves the color while adjusting intensity
            if (led.brightness < 255) {
                // Get the physical LED indices for this virtual index
                switch(i) {
                    case 0: // Bottom grip LED
                        gripLights.setLightBrightness(3, led.brightness);
                        break;
                    case 1:
                        gripLights.setLightBrightness(4, led.brightness);
                        break;
                    case 2:
                        gripLights.setLightBrightness(5, led.brightness);
                        break;
                    case 3: // Display LEDs
                        displayLights.setLightBrightness(11, led.brightness);
                        break;
                    case 4:
                        displayLights.setLightBrightness(10, led.brightness);
                        break;
                    case 5:
                        displayLights.setLightBrightness(9, led.brightness);
                        break;
                    case 6:
                        displayLights.setLightBrightness(8, led.brightness);
                        break;
                    case 7:
                        displayLights.setLightBrightness(7, led.brightness);
                        break;
                    case 8:
                        displayLights.setLightBrightness(6, led.brightness);
                        break;
                }
            }
        }
    }
    
    // Apply transmit light
    if (state.transmitLight.brightness > 0) {
        // Apply color at full intensity
        setLightColor(LightIdentifier::TRANSMIT_LIGHT, 0, state.transmitLight.color);
        
        // Then apply brightness separately
        if (state.transmitLight.brightness < 255) {
            displayLights.setLightBrightness(12, state.transmitLight.brightness);
        }
    }
} 