#pragma once

#include "image.hpp"

struct Cursor {
    int x = 0;
    int y = 0;
};

class Display {
public:

    virtual ~Display() = 0;

    virtual Display* invalidateScreen() = 0;

    virtual void render() = 0;

    virtual Display* drawText(char *text);

    virtual Display* drawText(char *text, int xStart, int yStart) = 0;

    virtual Display* drawImage(Image image) = 0;

    virtual Display* drawImage(Image image, int xStart, int yStart) = 0;

protected:
    Cursor cursor;
};
