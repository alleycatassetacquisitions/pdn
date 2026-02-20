#pragma once

struct Cursor {
    int x = 0;
    int y = 0;
};

enum class FontMode {
    TEXT,
    NUMBER_GLYPH,
    LOADING_GLYPH,
    TEXT_INVERTED_SMALL,
    TEXT_INVERTED_LARGE,
    ARCADE_1,
};

enum class ShapeType {
    RECTANGLE,
    CIRCLE,
    LINE,
    ELLIPSE,
    TRIANGLE,
};

class Shape {
public:
    Shape(ShapeType type, int x, int y, bool filled) : type(type), x(x), y(y), filled(filled) {}
    ShapeType type;
    int x;
    int y;
    bool filled;
};

class Circle : public Shape {
public:
    Circle(int x, int y, int radius, bool filled) : Shape(ShapeType::CIRCLE, x, y, filled), radius(radius) {}
    int radius;
};

class Line : public Shape {
public:
    Line(int startX, int startY, int endX, int endY) : Shape(ShapeType::LINE, startX, startY, false), endX(endX), endY(endY) {}
    int endX;
    int endY;
};

class Ellipse : public Shape {
public:
    Ellipse(int x, int y, int radiusX, int radiusY, bool filled) : Shape(ShapeType::ELLIPSE, x, y, filled), radiusX(radiusX), radiusY(radiusY) {}
    int radiusX;
    int radiusY;
};

class Triangle : public Shape {
public:
    Triangle(int x1, int y1, int x2, int y2, int x3, int y3, bool filled) : Shape(ShapeType::TRIANGLE, x1, y1, filled), x2(x2), y2(y2), x3(x3), y3(y3) {}
    int x2;
    int y2;
    int x3;
    int y3;
};

class Rectangle : public Shape {
public:
    Rectangle(int x, int y, int width, int height, bool filled) : Shape(ShapeType::RECTANGLE, x, y, filled), width(width), height(height) {}
    int width;
    int height;
};