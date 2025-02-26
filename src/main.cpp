#include <Arduino.h>
#include "device/pdn.hpp"

PDN* pdn = PDN::GetInstance();

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    
    Serial.println("Starting PDN...");
    pdn->begin();
    Serial.println("PDN initialized");

    // Start the idle animation
    AnimationConfig idleConfig;
    idleConfig.type = AnimationType::IDLE;
    idleConfig.loop = true;
    idleConfig.speed = 16;  // 16ms between updates
    
    pdn->startAnimation(idleConfig);
    Serial.println("Animation started");
    
    // Verify animation state
    if (pdn->isAnimating()) {
        Serial.println("Animation is running");
    } else {
        Serial.println("Animation failed to start");
    }
}

void loop() {
    pdn->loop();
}