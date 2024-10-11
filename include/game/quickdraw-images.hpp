//
// Created by Elli Furedy on 10/11/2024.
//
#pragma once

#include <map>
#include "images-raw.hpp"
#include "../../lib/pdn-libs/image.hpp"

using namespace std;


typedef map<ImageType, Image> ImageCollection;

ImageCollection alleycatImageCollection = {
    {ImageType::LOGO, Image(alleycatImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(alleycatImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(alleycatImages[indexStamp], 128, 64, 0, 0)},
{ImageType::CONNECT, Image(alleycatImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(alleycatImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(alleycatImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(alleycatImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(alleycatImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(alleycatImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(alleycatImages[indexLose], 128, 64, 0, 0)},
};

ImageCollection helixImageCollection = {
    {ImageType::LOGO, Image(helixImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(helixImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(helixImages[indexStamp], 128, 64, 0, 0)},
{ImageType::CONNECT, Image(helixImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(helixImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(helixImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(helixImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(helixImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(helixImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(helixImages[indexLose], 128, 64, 0, 0)},
};

ImageCollection endlineImageCollection = {
    {ImageType::LOGO, Image(endlineImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(endlineImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(endlineImages[indexStamp], 128, 64, 0, 0)},
{ImageType::CONNECT, Image(endlineImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(endlineImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(endlineImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(endlineImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(endlineImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(endlineImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(endlineImages[indexLose], 128, 64, 0, 0)},
};

ImageCollection resistanceImageCollection = {
    {ImageType::LOGO, Image(resistanceImages[indexLogo], 128, 64, 0, 0)},
{ImageType::IDLE, Image(resistanceImages[indexIdle], 128, 64, 0, 0)},
{ImageType::STAMP, Image(resistanceImages[indexStamp], 128, 64, 0, 0)},
{ImageType::CONNECT, Image(resistanceImages[indexConnect], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(resistanceImages[indexThree], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(resistanceImages[indexTwo], 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(resistanceImages[indexOne], 128, 64, 0, 0)},
{ImageType::DRAW, Image(resistanceImages[indexDraw], 128, 64, 0, 0)},
{ImageType::WIN, Image(resistanceImages[indexWin], 128, 64, 0, 0)},
{ImageType::LOSE, Image(resistanceImages[indexLose], 128, 64, 0, 0)},
};