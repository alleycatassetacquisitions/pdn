#include <Arduino.h>
#include "device/pdn.hpp"
#include "button.hpp"

// Global PDN instance
PDN* pdn = PDN::GetInstance();

// Function prototypes
void startIdleAnimation();
void startCountdownAnimation();
void onButtonClick();

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("PDN Animation Demo");
  
  // Initialize the PDN
  pdn->begin();
  
  // Start with idle animation
  startCountdownAnimation();
}

void loop() {
  // Update the PDN (handles animation)
  pdn->loop();
  
}

void startCountdownAnimation() {
  AnimationConfig config;
  config.type = AnimationType::COUNTDOWN;
  config.speed = 16;  // 16ms between frames (approximately 60fps)
  config.curve = EaseCurve::EASE_IN_OUT;
  
  Serial.println("Starting countdown animation...");
  pdn->startAnimation(config);
}