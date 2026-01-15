#include "light-manager.hpp"
#include "game/quickdraw-resources.hpp"  // For easing curve lookup tables
#include "device/idle-animation.hpp"
#include "device/countdown-animation.hpp"
#include "device/vertical-chase-animation.hpp"
#include "device/transmit-breath-animation.hpp"
#include "device/hunter-win-animation.hpp"
#include "device/bounty-win-animation.hpp"
#include "device/lose-animation.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"
#include <FastLED.h>

static const char* TAG = "LightManager";

LightManager::LightManager(LightStrip& pdnLights)
    : pdnLights(pdnLights)
    , currentAnimation(nullptr) {
}

LightManager::~LightManager() {
    if (currentAnimation) {
        delete currentAnimation;
    }
}

void LightManager::loop() {
    if (currentAnimation && !currentAnimation->isPaused()) {
        // Get the next frame from the animation
        LEDState state = currentAnimation->animate();
        
        // Apply the state to the physical LEDs
        applyLEDState(state);
    }
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
            currentAnimation = new CountdownAnimation();
            break;
        case AnimationType::LOSE:
            currentAnimation = new LoseAnimation();
            break;
        case AnimationType::HUNTER_WIN:
            currentAnimation = new HunterWinAnimation();
            break;
        case AnimationType::BOUNTY_WIN:
            currentAnimation = new BountyWinAnimation();
            break;
        case AnimationType::TRANSMIT_BREATH:
            currentAnimation = new TransmitBreathAnimation();
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
    FastLED.show();
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
    for (int i = 0; i < 6; i++) {
        gripLightArray[i] = LEDState::SingleLEDState(LEDColor(0,0,0), 0);
    }
    for (int i = 0; i < 13; i++) {
        displayLightArray[i] = LEDState::SingleLEDState(LEDColor(0,0,0), 0);
    }
    FastLED.show();
}

void LightManager::setGlobalBrightness(uint8_t brightness) {
    FastLED.setBrightness(brightness);
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

void LightManager::mapStateToGripLights(const LEDState& state) {
    // Map the LEDState to the grip lights array according to the pattern
    gripLightArray[0] = state.leftLights[2];
    gripLightArray[1] = state.leftLights[1];
    gripLightArray[2] = state.leftLights[0];
    gripLightArray[3] = state.rightLights[0];
    gripLightArray[4] = state.rightLights[1];
    gripLightArray[5] = state.rightLights[2];
}

void LightManager::mapStateToDisplayLights(const LEDState& state) {
    // Map the LEDState to the display lights array according to the pattern
    displayLightArray[0] = state.leftLights[3];
    displayLightArray[1] = state.leftLights[4];
    displayLightArray[2] = state.leftLights[5];
    displayLightArray[3] = state.leftLights[6];
    displayLightArray[4] = state.leftLights[7];
    displayLightArray[5] = state.leftLights[8];
    displayLightArray[6] = state.rightLights[8];
    displayLightArray[7] = state.rightLights[7];
    displayLightArray[8] = state.rightLights[6];
    displayLightArray[9] = state.rightLights[5];
    displayLightArray[10] = state.rightLights[4];
    displayLightArray[11] = state.rightLights[3];
    displayLightArray[12] = state.transmitLight;
}

void LightManager::applyLEDState(const LEDState& state) {
    // Extract the grip and display lights from the LEDState
    mapStateToGripLights(state);
    mapStateToDisplayLights(state);
    
    // Apply the grip lights
    for (int i = 0; i < 6; i++) {
        this->pdnLights.setLight(LightIdentifier::GRIP_LIGHTS, i, gripLightArray[i]);
    }
    
    // Apply the display lights
    for (int i = 0; i < 13; i++) {
        this->pdnLights.setLight(LightIdentifier::DISPLAY_LIGHTS, i, displayLightArray[i]);
    }
}