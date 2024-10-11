//
// Created by Elli Furedy on 10/11/2024.
//
#pragma once

enum class ImageType {
    LOGO = 0,
    IDLE = 1,
    STAMP = 2,
    CONNECT = 3,
    COUNTDOWN_THREE = 4,
    COUNTDOWN_TWO = 5,
    COUNTDOWN_ONE = 6,
    DRAW = 7,
    WIN = 8,
    LOSE = 9,
};

struct Image {
    Image(const unsigned char* rawImage, int width, int height, int defaultStartX, int defaultStartY) {
        this->rawImage = rawImage;
        this->width = width;
        this->height = height;
        this->defaultStartX = defaultStartX;
        this->defaultStartY = defaultStartY;
    }

    const unsigned char* rawImage;
    int width;
    int height;
    int defaultStartX;
    int defaultStartY;
};