//
// Created by Elli Furedy on 10/11/2024.
//
#ifndef QUICKDRAW_RESOURCES_H
#define QUICKDRAW_RESOURCES_H

#include <map>
#include "images-raw.hpp"
#include "image.hpp"
#include "player.hpp"
#include <FastLED.h>

using namespace std;

typedef std::map<ImageType, Image> ImageCollection;

const ImageCollection alleycatImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_logo_alley, 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(image_logo_alley, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_alley_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_alley_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_alley_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_alley_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_alley_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_alley_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_alley_draw, 128, 64, 0, 0)},
{ImageType::WIN, Image(image_alley_victor, 128, 64, 0, 0)},
{ImageType::LOSE, Image(image_alley_loser, 128, 64, 0, 0)},
};

const ImageCollection helixImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_logo_helix, 128, 64, 64, 0)},
    {ImageType::LOGO_LEFT, Image(image_logo_helix, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_helix_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_helix_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_helix_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_helix_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_helix_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_helix_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_helix_draw, 128, 64, 0, 0)},
{ImageType::WIN, Image(image_helix_victor, 128, 64, 0, 0)},
{ImageType::LOSE, Image(image_helix_loser, 128, 64, 0, 0)},
};

const ImageCollection endlineImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_logo_endline, 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(image_logo_endline, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_endline_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_endline_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_endline_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_endline_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_endline_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_endline_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_endline_draw, 128, 64, 0, 0)},
{ImageType::WIN, Image(image_endline_victor, 128, 64, 0, 0)},
{ImageType::LOSE, Image(image_endline_loser, 128, 64, 0, 0)},
};

const ImageCollection resistanceImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_resistance_stamp, 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(image_resistance_stamp, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_resistance_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_resistance_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_resistance_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_resistance_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_resistance_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_resistance_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_resistance_draw, 128, 64, 0, 0)},
{ImageType::WIN, Image(image_resistance_victor, 128, 64, 0, 0)},
{ImageType::LOSE, Image(image_resistance_loser, 128, 64, 0, 0)},
};

const CRGBPalette16 bountyColors = CRGBPalette16(
    CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Orange, CRGB::Red, CRGB::Red,
    CRGB::Red, CRGB::Orange, CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red,
    CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red);

const CRGBPalette16 hunterColors = CRGBPalette16(
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen);

const CRGBPalette16 idleColors = CRGBPalette16(
    CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow,
    CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow
);

// Idle animation colors for bounty and hunter
const CRGB bountyIdleColors[4] = {
    CRGB(255,2,1),    // Color 1
    CRGB(237,75,0),   // Color 2 (7% dimmer)
    CRGB(255,51,0),   // Color 3
    CRGB(222,97,7)    // Color 4 (13% dimmer)
};

const CRGB hunterIdleColors[4] = {
    CRGB(0,255,0),    // Green
    CRGB(0,237,75),   // Blue-green
    CRGB(0,200,100),  // Teal
    CRGB(0,180,180)   // Cyan
};

// Pre-calculated curve values for smooth transitions (0-255)

// Linear curve (t)
const uint8_t LINEAR_CURVE[256] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
    208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
    224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
};

// Smooth ease-in-out curve (t * t * (3 - 2 * t))
const uint8_t EASE_IN_OUT_CURVE[256] = {
    0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 15, 16,
    18, 19, 21, 22, 24, 26, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45,
    47, 49, 51, 54, 56, 58, 61, 63, 66, 68, 71, 73, 76, 79, 81, 84,
    87, 90, 93, 96, 99, 102, 105, 108, 111, 114, 117, 120, 124, 127, 130, 134,
    137, 141, 144, 148, 151, 155, 158, 162, 166, 169, 173, 177, 181, 185, 189, 193,
    197, 201, 205, 209, 213, 217, 221, 225, 229, 234, 238, 242, 246, 250, 255, 255,
    255, 255, 250, 246, 242, 238, 234, 229, 225, 221, 217, 213, 209, 205, 201, 197,
    193, 189, 185, 181, 177, 173, 169, 166, 162, 158, 155, 151, 148, 144, 141, 137,
    134, 130, 127, 124, 120, 117, 114, 111, 108, 105, 102, 99, 96, 93, 90, 87,
    84, 81, 79, 76, 73, 71, 68, 66, 63, 61, 58, 56, 54, 51, 49, 47,
    45, 43, 41, 39, 37, 35, 33, 31, 29, 27, 26, 24, 22, 21, 19, 18,
    16, 15, 13, 12, 11, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Quick start, gradual slowdown (1 - (1-t)^4)
const uint8_t EASE_OUT_CURVE[256] = {
    0, 38, 64, 84, 102, 117, 130, 143, 153, 163, 171, 179, 186, 194, 199, 204,
    209, 214, 217, 222, 224, 227, 230, 232, 235, 237, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

// Elastic/bouncy curve (overshoots and settles)
const uint8_t ELASTIC_CURVE[256] = {
    0, 5, 10, 20, 31, 41, 51, 61, 71, 82, 92, 102, 112, 122, 133, 143,
    153, 163, 173, 184, 194, 204, 214, 224, 235, 245, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 250, 242, 235, 227,
    222, 217, 212, 209, 207, 204, 207, 209, 212, 217, 222, 227, 232, 237, 242, 247,
    252, 255, 255, 255, 255, 255, 255, 255, 255, 255, 252, 250, 247, 245, 242, 240,
    240, 242, 245, 247, 250, 252, 255, 255, 255, 255, 255, 255, 255, 252, 250, 247,
    247, 247, 250, 252, 255, 255, 255, 255, 255, 252, 250, 250, 250, 252, 255, 255,
    255, 255, 255, 252, 252, 252, 252, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

// Countdown animation implementation for future use
/*
void countdownAnimation(Device *PDN) {
    static uint8_t currentLed = 3;        // Starting LED index (3-8)
    static uint8_t fadeProgress = 0;      // Progress of current LED's fade (0-255)
    static bool isWaitingBetweenLeds = false;
    static SimpleTimer ledDelayTimer;
    
    // If all LEDs are lit, keep them all at full brightness
    if (currentLed >= 9) {
        // Keep all LEDs at full brightness
        for (int i = 3; i <= 8; i++) {
            PDN->setLight(LightIdentifier::LEFT_LIGHTS, i, LEDColor(255, 255, 255));
            PDN->setLight(LightIdentifier::RIGHT_LIGHTS, i, LEDColor(255, 255, 255));
        }
        return;
    }

    // If we're in the delay period between LEDs
    if (isWaitingBetweenLeds) {
        if (ledDelayTimer.expired()) {
            isWaitingBetweenLeds = false;
            currentLed++;           // Move to next LED
            fadeProgress = 0;       // Reset fade progress for new LED
        }
        return;
    }

    // Get smooth fade value from ease-in-out curve
    float brightness = EASE_IN_OUT_CURVE[fadeProgress];
    uint8_t ledValue = brightness * 255;

    // Set current LED brightness
    PDN->setLight(LightIdentifier::LEFT_LIGHTS, currentLed, LEDColor(ledValue, ledValue, ledValue));
    PDN->setLight(LightIdentifier::RIGHT_LIGHTS, currentLed, LEDColor(ledValue, ledValue, ledValue));

    // Keep previous LEDs at full brightness
    for (int i = 3; i < currentLed; i++) {
        PDN->setLight(LightIdentifier::LEFT_LIGHTS, i, LEDColor(255, 255, 255));
        PDN->setLight(LightIdentifier::RIGHT_LIGHTS, i, LEDColor(255, 255, 255));
    }

    // Update fade progress
    fadeProgress += 5;  // Adjust this value to control fade speed

    // When fade is complete, start delay before next LED
    if (fadeProgress >= 255) {
        isWaitingBetweenLeds = true;
        ledDelayTimer.setTimer(200);  // 200ms delay between LEDs
    }
}
*/

static const ImageCollection* getCollectionForAllegiance(Allegiance allegiance) {
    switch(allegiance) {
        case Allegiance::HELIX:
            return &helixImageCollection;
        case Allegiance::ENDLINE:
            return &endlineImageCollection;
        case Allegiance::RESISTANCE:
            return &resistanceImageCollection;
        default:
            return &alleycatImageCollection;
    }
}

// void IdleAnimation(Device *PDN) {
// if (animState.isWaitingForPause) {
//         if (animState.pauseTimer.expired()) {
//             animState.isWaitingForPause = false;
//             animState.currentIndex = 8;  // Reset to top
//             animState.prevIndex = 8;
//             animState.brightness = 0;
//         }
//         return;
//     }

//     // Clear all LEDs except our controlled ones
//     setBrightness(LightIdentifier::GRIP_LIGHTS, 0);
//     setBrightness(LightIdentifier::DISPLAY_LIGHTS, 0);
    
//     if (animState.isFadingOut) {
//         // Only show the bottom LED fading out
//         // Decrease brightness for fade out
//         if (animState.brightness >= 45) {
//             animState.brightness -= 45;
//         } else {
//             animState.brightness = 0;
//         }

//         setLightColor(LightIdentifier::LEFT_LIGHTS, 0, LEDColor(animState.brightness, animState.brightness, animState.brightness));
//         setLightColor(LightIdentifier::RIGHT_LIGHTS, 0, LEDColor(animState.brightness, animState.brightness, animState.brightness));
        
//         if (animState.brightness == 0) {
//             animState.isFadingOut = false;
//             animState.isWaitingForPause = true;
//             animState.pauseTimer.setTimer(250);  // 250ms pause
//         }
//         return;
//     }
    
//     // Normal animation sequence
//     // Set the current LED pair to white with increasing brightness
//     setLightColor(LightIdentifier::LEFT_LIGHTS, animState.currentIndex, LEDColor(animState.brightness, animState.brightness, animState.brightness));
//     setLightColor(LightIdentifier::RIGHT_LIGHTS, animState.currentIndex, LEDColor(animState.brightness, animState.brightness, animState.brightness));
    
//     // Set the previous LED pair with decreasing brightness
//     if (animState.prevIndex != animState.currentIndex) {  // Only if we have a different previous position
//         uint8_t prevBrightness = 255 - animState.brightness;  // Inverse fade
//         setLightColor(LightIdentifier::LEFT_LIGHTS, animState.prevIndex, LEDColor(prevBrightness, prevBrightness, prevBrightness));
//         setLightColor(LightIdentifier::RIGHT_LIGHTS, animState.prevIndex, LEDColor(prevBrightness, prevBrightness, prevBrightness));
//     }
    
//     static const uint8_t brightnessStep = 45;  // Configurable step size
//     if (animState.brightness <= (255 - brightnessStep)) {  // Prevent overflow
//         animState.brightness += brightnessStep;
//     } else {
//         animState.brightness = 255;  // Ensure we hit exactly 255
//     }
    
//     // When we reach full brightness, move to next position
//     if (animState.brightness >= 255) {
//         if (animState.currentIndex == 0) {  // If we're at the bottom
//             animState.isFadingOut = true;   // Start the fade out sequence
//         } else {
//             animState.brightness = 0;  // Reset brightness for next position
//             animState.prevIndex = animState.currentIndex;  // Store current position as previous
//             animState.currentIndex--;  // Move to next position (moving downward)
//         }
//     }
// }

#endif