#include "shape.h"

int main() {
    Shape *shape = makeSquare(3);
    int result = shape->surface();
    delete shape;
    return result == 9 ? 0 : 1;
}
