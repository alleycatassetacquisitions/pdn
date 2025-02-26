#include <Arduino.h>
#include "device/pdn.hpp"
#include "esp_log.h"

static const char* TAG = "PDN_MAIN";

PDN* pdn = PDN::GetInstance();

void setup() {
    ESP_LOGI(TAG, "Starting PDN...");
    pdn->begin();
    ESP_LOGI(TAG, "PDN initialized");

    // Start the idle animation
    AnimationConfig idleConfig;
    idleConfig.type = AnimationType::IDLE;
    idleConfig.loop = true;
    idleConfig.speed = 16;  // 16ms between updates
    
    pdn->startAnimation(idleConfig);
    ESP_LOGI(TAG, "Animation started");
    
    // Verify animation state
    if (pdn->isAnimating()) {
        ESP_LOGI(TAG, "Animation is running");
    } else {
        ESP_LOGE(TAG, "Animation failed to start");
    }
}

// Helper to convert animation type to string
const char* getAnimationTypeName(AnimationType type) {
    switch (type) {
        case AnimationType::IDLE:
            return "IDLE";
        case AnimationType::DEVICE_CONNECTED:
            return "DEVICE_CONNECTED";
        case AnimationType::COUNTDOWN:
            return "COUNTDOWN";
        case AnimationType::LOSE:
            return "LOSE";
        case AnimationType::WIN:
            return "WIN";
        default:
            return "UNKNOWN";
    }
}

void loop() {
    static uint32_t lastDebugTime = 0;
    const uint32_t debugInterval = 1000;  // Print debug info every second
    
    pdn->loop();
    
    // Print debug info periodically
    uint32_t now = millis();
    if (now - lastDebugTime >= debugInterval) {
        lastDebugTime = now;
        
        // Build status string
        char status[128];
        snprintf(status, sizeof(status), "Animation Status - Running: %s, Paused: %s, Type: %s",
            pdn->isAnimating() ? "Yes" : "No",
            pdn->isPaused() ? "Yes" : "No",
            getAnimationTypeName(pdn->getCurrentAnimation()));
            
        ESP_LOGI(TAG, "%s", status);
    }
}

