#pragma once

struct Shape {
    virtual ~Shape() = default;
    virtual int surface() const = 0;
};

Shape *makeSquare(int side);
