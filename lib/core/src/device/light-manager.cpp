#include "device/light-manager.hpp"
#include <algorithm> // For std::min

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

void LightManager::startAnimation(AnimationBase* animation, AnimationConfig config) {
    if (currentAnimation) {
        delete currentAnimation;
    }
    currentAnimation = animation;
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
    clear();
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

void LightManager::clear() {
    applyLEDState(LEDState());
}

void LightManager::setGlobalBrightness(uint8_t brightness) {
    pdnLights.setGlobalBrightness(brightness);
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