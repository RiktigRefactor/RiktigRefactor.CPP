#include "shape.h"

int main() {
    Shape *shape = makeSquare(3);
    int result = shape->area();
    delete shape;
    return result == 9 ? 0 : 1;
}
