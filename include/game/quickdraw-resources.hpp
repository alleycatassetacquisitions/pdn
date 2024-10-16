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
#endif