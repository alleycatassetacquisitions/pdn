//
// Created by Elli Furedy on 10/11/2024.
//
#pragma once

#include <map>
#include "images-raw.hpp"
#include "image.hpp"
#include <FastLED.h>

using namespace std;

typedef map<ImageType, Image> ImageCollection;

ImageCollection alleycatImageCollection = {
    {ImageType::LOGO_RIGHT, Image(alleycatImages[indexLogo], 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(alleycatImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(alleycatImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(alleycatImages[indexStamp], 128, 64, 64, 0)},
{ImageType::CONNECT, Image(alleycatImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(alleycatImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(alleycatImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(alleycatImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(alleycatImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(alleycatImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(alleycatImages[indexLose], 128, 64, 0, 0)},
};

ImageCollection helixImageCollection = {
    {ImageType::LOGO_RIGHT, Image(helixImages[indexLogo], 128, 64, 64, 0)},
    {ImageType::LOGO_LEFT, Image(helixImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(helixImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(helixImages[indexStamp], 128, 64, 64, 0)},
{ImageType::CONNECT, Image(helixImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(helixImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(helixImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(helixImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(helixImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(helixImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(helixImages[indexLose], 128, 64, 0, 0)},
};

ImageCollection endlineImageCollection = {
    {ImageType::LOGO_RIGHT, Image(endlineImages[indexLogo], 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(endlineImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(endlineImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(endlineImages[indexStamp], 128, 64, 64, 0)},
{ImageType::CONNECT, Image(endlineImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(endlineImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(endlineImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(endlineImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(endlineImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(endlineImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(endlineImages[indexLose], 128, 64, 0, 0)},
};

ImageCollection resistanceImageCollection = {
    {ImageType::LOGO_RIGHT, Image(resistanceImages[indexLogo], 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(resistanceImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(resistanceImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(resistanceImages[indexStamp], 128, 64, 64, 0)},
{ImageType::CONNECT, Image(resistanceImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(resistanceImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(resistanceImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(resistanceImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(resistanceImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(resistanceImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(resistanceImages[indexLose], 128, 64, 0, 0)},
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