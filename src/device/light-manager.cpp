#include "device/light-manager.hpp"

LightManager::LightManager(DisplayLights& displayLights, GripLights& gripLights)
    : displayLights(displayLights)
    , gripLights(gripLights)
    , palette(nullptr)
    , paletteSize(0) {
    animState = {};
}

LightManager::~LightManager() {
    cleanupPalette();
}

void LightManager::begin() {
    // Initialize animation state
    animState = {};
}

void LightManager::loop() {
    if (animState.isActive && !animState.isPaused) {
        updateAnimation();
    }
    
    // Allow the light strips to update their state
    displayLights.loop();
    gripLights.loop();
    
    // Update the physical LEDs
    FastLED.show();
}

void LightManager::startAnimation(AnimationConfig config) {
    stopAnimation();
    animState.config = config;
    animState.isActive = true;
    animState.isPaused = false;
    animState.currentStep = 0;
    animState.currentIndex = 8;  // Start at top (index 8)
    animState.prevIndex = 8;
    animState.brightness = 0;
    animState.lastUpdate = millis();
}

void LightManager::stopAnimation() {
    animState.isActive = false;
    clear(LightIdentifier::DISPLAY_LIGHTS);
    clear(LightIdentifier::GRIP_LIGHTS);
}

void LightManager::pauseAnimation() {
    animState.isPaused = true;
}

void LightManager::resumeAnimation() {
    animState.isPaused = false;
    animState.lastUpdate = millis();
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
    cleanupPalette();
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
    return animState.isActive;
}

bool LightManager::isPaused() const {
    return animState.isPaused;
}

AnimationType LightManager::getCurrentAnimation() const {
    return animState.config.type;
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

void LightManager::updateAnimation() {
    if (!animState.isActive || animState.isPaused) return;

    uint32_t now = millis();
    if (now - animState.lastUpdate < animState.config.speed) return;
    animState.lastUpdate = now;

    switch (animState.config.type) {
        case AnimationType::IDLE:
            updateIdleAnimation();
            break;
        case AnimationType::DEVICE_CONNECTED:
            updateDeviceConnectedAnimation();
            break;
        case AnimationType::COUNTDOWN:
            updateCountdownAnimation();
            break;
        case AnimationType::LOSE:
            updateLoseAnimation();
            break;
        case AnimationType::WIN:
            updateWinAnimation();
            break;
    }
}

void LightManager::updateIdleAnimation() {
    // If we're in the pause state
    if (animState.isWaitingForPause) {
        if (animState.pauseTimer.expired()) {
            animState.isWaitingForPause = false;
            animState.currentIndex = 8;  // Reset to top
            animState.prevIndex = 8;
            animState.brightness = 0;
        }
        return;
    }

    // Clear all LEDs except our controlled ones
    setBrightness(LightIdentifier::GRIP_LIGHTS, 0);
    setBrightness(LightIdentifier::DISPLAY_LIGHTS, 0);
    
    if (animState.isFadingOut) {
        // Only show the bottom LED fading out
        // Decrease brightness for fade out
        if (animState.brightness >= 45) {
            animState.brightness -= 45;
        } else {
            animState.brightness = 0;
        }

        setLightColor(LightIdentifier::LEFT_LIGHTS, 0, LEDColor(animState.brightness, animState.brightness, animState.brightness));
        setLightColor(LightIdentifier::RIGHT_LIGHTS, 0, LEDColor(animState.brightness, animState.brightness, animState.brightness));
        
        if (animState.brightness == 0) {
            animState.isFadingOut = false;
            animState.isWaitingForPause = true;
            animState.pauseTimer.setTimer(250);  // 250ms pause
        }
        return;
    }
    
    // Normal animation sequence
    // Set the current LED pair to white with increasing brightness
    setLightColor(LightIdentifier::LEFT_LIGHTS, animState.currentIndex, LEDColor(animState.brightness, animState.brightness, animState.brightness));
    setLightColor(LightIdentifier::RIGHT_LIGHTS, animState.currentIndex, LEDColor(animState.brightness, animState.brightness, animState.brightness));
    
    // Set the previous LED pair with decreasing brightness
    if (animState.prevIndex != animState.currentIndex) {  // Only if we have a different previous position
        uint8_t prevBrightness = 255 - animState.brightness;  // Inverse fade
        setLightColor(LightIdentifier::LEFT_LIGHTS, animState.prevIndex, LEDColor(prevBrightness, prevBrightness, prevBrightness));
        setLightColor(LightIdentifier::RIGHT_LIGHTS, animState.prevIndex, LEDColor(prevBrightness, prevBrightness, prevBrightness));
    }
    
    static const uint8_t brightnessStep = 45;  // Configurable step size
    if (animState.brightness <= (255 - brightnessStep)) {  // Prevent overflow
        animState.brightness += brightnessStep;
    } else {
        animState.brightness = 255;  // Ensure we hit exactly 255
    }
    
    // When we reach full brightness, move to next position
    if (animState.brightness >= 255) {
        if (animState.currentIndex == 0) {  // If we're at the bottom
            animState.isFadingOut = true;   // Start the fade out sequence
        } else {
            animState.brightness = 0;  // Reset brightness for next position
            animState.prevIndex = animState.currentIndex;  // Store current position as previous
            animState.currentIndex--;  // Move to next position (moving downward)
        }
    }
}

void LightManager::updateDeviceConnectedAnimation() {
    // TODO: Implement custom device connected animation
}

void LightManager::updateCountdownAnimation() {
    // TODO: Implement custom countdown animation
}

void LightManager::updateLoseAnimation() {
    // TODO: Implement custom lose animation
}

void LightManager::updateWinAnimation() {
    // TODO: Implement custom win animation
}

void LightManager::cleanupPalette() {
    if (palette) {
        delete[] palette;
        palette = nullptr;
        paletteSize = 0;
    }
} 