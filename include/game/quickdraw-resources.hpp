//
// Created by Elli Furedy on 10/11/2024.
//
#ifndef QUICKDRAW_RESOURCES_H
#define QUICKDRAW_RESOURCES_H

#include <map>
#include "images-raw.hpp"
#include "image.hpp"
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

// Pre-calculated curve values for smooth transitions

// Smooth ease-in-out curve (t * t * (3 - 2 * t))
const float EASE_IN_OUT_CURVE[256] = {
    0.0f, 0.0f, 0.004f, 0.008f, 0.012f, 0.016f, 0.02f, 0.024f, 0.027f, 0.031f, 0.035f, 0.043f, 0.047f, 0.051f, 0.059f, 0.063f,
    0.071f, 0.075f, 0.082f, 0.086f, 0.094f, 0.102f, 0.106f, 0.114f, 0.122f, 0.129f, 0.137f, 0.145f, 0.153f, 0.161f, 0.169f, 0.176f,
    0.184f, 0.192f, 0.2f, 0.212f, 0.22f, 0.227f, 0.239f, 0.247f, 0.259f, 0.267f, 0.278f, 0.286f, 0.298f, 0.31f, 0.318f, 0.329f,
    0.341f, 0.353f, 0.365f, 0.376f, 0.388f, 0.4f, 0.412f, 0.424f, 0.435f, 0.447f, 0.459f, 0.471f, 0.486f, 0.498f, 0.51f, 0.525f,
    0.537f, 0.553f, 0.565f, 0.58f, 0.592f, 0.608f, 0.62f, 0.635f, 0.651f, 0.663f, 0.678f, 0.694f, 0.71f, 0.725f, 0.741f, 0.757f,
    0.773f, 0.788f, 0.804f, 0.82f, 0.835f, 0.851f, 0.867f, 0.882f, 0.898f, 0.918f, 0.933f, 0.949f, 0.965f, 0.98f, 1.0f, 1.0f,
    1.0f, 1.0f, 0.98f, 0.965f, 0.949f, 0.933f, 0.918f, 0.898f, 0.882f, 0.867f, 0.851f, 0.835f, 0.82f, 0.804f, 0.788f, 0.773f,
    0.757f, 0.741f, 0.725f, 0.71f, 0.694f, 0.678f, 0.663f, 0.651f, 0.635f, 0.62f, 0.608f, 0.592f, 0.58f, 0.565f, 0.553f, 0.537f,
    0.525f, 0.51f, 0.498f, 0.486f, 0.471f, 0.459f, 0.447f, 0.435f, 0.424f, 0.412f, 0.4f, 0.388f, 0.376f, 0.365f, 0.353f, 0.341f,
    0.329f, 0.318f, 0.31f, 0.298f, 0.286f, 0.278f, 0.267f, 0.259f, 0.247f, 0.239f, 0.227f, 0.22f, 0.212f, 0.2f, 0.192f, 0.184f,
    0.176f, 0.169f, 0.161f, 0.153f, 0.145f, 0.137f, 0.129f, 0.122f, 0.114f, 0.106f, 0.102f, 0.094f, 0.086f, 0.082f, 0.075f, 0.071f,
    0.063f, 0.059f, 0.051f, 0.047f, 0.043f, 0.035f, 0.031f, 0.027f, 0.024f, 0.02f, 0.016f, 0.012f, 0.008f, 0.004f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
};

// Quick start, gradual slowdown (1 - (1-t)^4)
const float EASE_OUT_CURVE[256] = {
    0.0f, 0.15f, 0.25f, 0.33f, 0.4f, 0.46f, 0.51f, 0.56f, 0.6f, 0.64f, 0.67f, 0.7f, 0.73f, 0.76f, 0.78f, 0.8f,
    0.82f, 0.84f, 0.85f, 0.87f, 0.88f, 0.89f, 0.9f, 0.91f, 0.92f, 0.93f, 0.93f, 0.94f, 0.94f, 0.95f, 0.95f, 0.96f,
    0.96f, 0.96f, 0.97f, 0.97f, 0.97f, 0.97f, 0.98f, 0.98f, 0.98f, 0.98f, 0.98f, 0.99f, 0.99f, 0.99f, 0.99f, 0.99f,
    0.99f, 0.99f, 0.99f, 0.99f, 0.99f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};

// Elastic/bouncy curve (overshoots and settles)
const float ELASTIC_CURVE[256] = {
    0.0f, 0.02f, 0.04f, 0.08f, 0.12f, 0.16f, 0.2f, 0.24f, 0.28f, 0.32f, 0.36f, 0.4f, 0.44f, 0.48f, 0.52f, 0.56f,
    0.6f, 0.64f, 0.68f, 0.72f, 0.76f, 0.8f, 0.84f, 0.88f, 0.92f, 0.96f, 1.0f, 1.04f, 1.08f, 1.12f, 1.15f, 1.17f,
    1.18f, 1.19f, 1.2f, 1.19f, 1.18f, 1.17f, 1.15f, 1.13f, 1.1f, 1.07f, 1.04f, 1.01f, 0.98f, 0.95f, 0.92f, 0.89f,
    0.87f, 0.85f, 0.83f, 0.82f, 0.81f, 0.8f, 0.81f, 0.82f, 0.83f, 0.85f, 0.87f, 0.89f, 0.91f, 0.93f, 0.95f, 0.97f,
    0.99f, 1.01f, 1.02f, 1.03f, 1.04f, 1.05f, 1.04f, 1.03f, 1.02f, 1.01f, 0.99f, 0.98f, 0.97f, 0.96f, 0.95f, 0.94f,
    0.94f, 0.95f, 0.96f, 0.97f, 0.98f, 0.99f, 1.0f, 1.01f, 1.02f, 1.02f, 1.02f, 1.01f, 1.0f, 0.99f, 0.98f, 0.97f,
    0.97f, 0.97f, 0.98f, 0.99f, 1.0f, 1.01f, 1.01f, 1.01f, 1.0f, 0.99f, 0.98f, 0.98f, 0.98f, 0.99f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 0.99f, 0.99f, 0.99f, 0.99f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
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

// void Idle::gripChase(Device *PDN) {
//     // If we're in the delay period between cycles
//     if (isWaitingBetweenCycles) {
//         if (cycleDelayTimer.expired()) {
//             isWaitingBetweenCycles = false;
//             transitionProgress = 0;
//             ledChaseIndex = 0;
//         }
//         return;
//     }

//     // Use the appropriate color array based on player type
//     const CRGB* colors = player->isHunter() ? hunterIdleColors : bountyIdleColors;
    
//     // Calculate indices for each LED based on the chase index
//     int firstLedColor = (ledChaseIndex + 0) % 4;
//     int secondLedColor = (ledChaseIndex + 1) % 4;
//     int thirdLedColor = (ledChaseIndex + 2) % 4;
    
//     // Calculate next color indices for interpolation
//     int nextFirstLedColor = (firstLedColor + 1) % 4;
//     int nextSecondLedColor = (secondLedColor + 1) % 4;
//     int nextThirdLedColor = (thirdLedColor + 1) % 4;
    
//     // Get the bezier curve value from the lookup table
//     float bezierT = BEZIER_LOOKUP[transitionProgress];
    
//     // Interpolate between current and next colors
//     auto lerpColor = [](const CRGB& start, const CRGB& end, float t) {
//         return CRGB(
//             start.r + (end.r - start.r) * t,
//             start.g + (end.g - start.g) * t,
//             start.b + (end.b - start.b) * t
//         );
//     };
    
//     // Calculate interpolated colors using bezier curve
//     CRGB firstLedFinal = lerpColor(colors[firstLedColor], colors[nextFirstLedColor], bezierT);
//     CRGB secondLedFinal = lerpColor(colors[secondLedColor], colors[nextSecondLedColor], bezierT);
//     CRGB thirdLedFinal = lerpColor(colors[thirdLedColor], colors[nextThirdLedColor], bezierT);
    
//     // Set the LEDs with interpolated colors
//     PDN->setLight(LightIdentifier::LEFT_LIGHTS, 0, LEDColor(firstLedFinal.r, firstLedFinal.g, firstLedFinal.b));
//     PDN->setLight(LightIdentifier::LEFT_LIGHTS, 1, LEDColor(secondLedFinal.r, secondLedFinal.g, secondLedFinal.b));
//     PDN->setLight(LightIdentifier::LEFT_LIGHTS, 2, LEDColor(thirdLedFinal.r, thirdLedFinal.g, thirdLedFinal.b));
    
//     PDN->setLight(LightIdentifier::RIGHT_LIGHTS, 0, LEDColor(firstLedFinal.r, firstLedFinal.g, firstLedFinal.b));
//     PDN->setLight(LightIdentifier::RIGHT_LIGHTS, 1, LEDColor(secondLedFinal.r, secondLedFinal.g, secondLedFinal.b));
//     PDN->setLight(LightIdentifier::RIGHT_LIGHTS, 2, LEDColor(thirdLedFinal.r, thirdLedFinal.g, thirdLedFinal.b));

//     // Update transition progress
//     transitionProgress += 1; // Adjust this value to control transition speed
    
//     // When transition is complete, move to next color
//     if (transitionProgress >= 255) {
//         transitionProgress = 0;
//         ledChaseIndex++;
//         if(ledChaseIndex >= 4) {
//             // Start the delay between cycles
//             isWaitingBetweenCycles = true;
//             cycleDelayTimer.setTimer(50); // 50ms delay
//         }
//     }
// }

#endif