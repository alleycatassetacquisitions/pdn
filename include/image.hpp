//
// Created by Elli Furedy on 10/11/2024.
//
#pragma once

enum class ImageType {
    LOGO_RIGHT = 0,
    LOGO_LEFT = 1,
    IDLE = 2,
    STAMP = 3,
    CONNECT = 4,
    COUNTDOWN_THREE = 5,
    COUNTDOWN_TWO = 6,
    COUNTDOWN_ONE = 7,
    DRAW = 8,
    WIN = 9,
    LOSE = 10,
};

struct Image {
    Image() : rawImage(nullptr), width(0), height(0), defaultStartX(0), defaultStartY(0), name(nullptr) {}

    Image(const unsigned char* rawImage, int width, int height,
          int defaultStartX, int defaultStartY, const char* name = nullptr) {
        this->rawImage = rawImage;
        this->width = width;
        this->height = height;
        this->defaultStartX = defaultStartX;
        this->defaultStartY = defaultStartY;
        this->name = name;
    }

    const unsigned char* rawImage;
    int width;
    int height;
    int defaultStartX;
    int defaultStartY;
    const char* name;
};