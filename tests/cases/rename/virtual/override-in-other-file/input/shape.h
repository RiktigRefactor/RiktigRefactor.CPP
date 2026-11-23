#pragma once

struct Shape {
    virtual ~Shape() = default;
    virtual int /*|*/area() const = 0;
};

Shape *makeSquare(int side);
