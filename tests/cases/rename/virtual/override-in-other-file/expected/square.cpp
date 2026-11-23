#include "shape.h"

struct Square : Shape {
    explicit Square(int side) : side(side) {}
    int surface() const override { return side * side; }
    int side;
};

Shape *makeSquare(int side) { return new Square(side); }
