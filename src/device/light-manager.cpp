#include "device/light-manager.hpp"
#include "game/quickdraw-resources.hpp"  // For easing curve lookup tables
#include "device/idle-animation.hpp"
#include "device/countdown-animation.hpp"
#include "device/vertical-chase-animation.hpp"
#include "device/transmit-breath-animation.hpp"
#include "device/hunter-win-animation.hpp"
#include "device/bounty-win-animation.hpp"
#include "device/lose-animation.hpp"
#include <algorithm> // For std::min

static const char* TAG = "LightManager";

LightManager::LightManager(LightStrip& pdnLights)
    : lights(pdnLights)
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
    lights.setGlobalBrightness(brightness);
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

void LightManager::mapStateToRecessLights(const LEDState& state) {
    for (int i = 0; i < 23; i++) {
        recessLightArray[i] = state.recessLights[i];
    }
}

void LightManager::mapStateToFinLights(const LEDState& state) {
    for (int i = 0; i < 9; i++) {
        finLightArray[i] = state.finLights[i];
    }
}

void LightManager::applyLEDState(const LEDState& state) {
    mapStateToRecessLights(state);
    mapStateToFinLights(state);
    
    for (int i = 0; i < 23; i++) {
        lights.setLight(LightIdentifier::RECESS_LIGHTS, i, recessLightArray[i]);
    }
    
    for (int i = 0; i < 9; i++) {
        lights.setLight(LightIdentifier::FIN_LIGHTS, i, finLightArray[i]);
    }
}