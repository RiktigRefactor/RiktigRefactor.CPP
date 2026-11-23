#include "shapes.h"

namespace alias = geometry;

int count() {
    using namespace geometry;
    return sides();
}

int main() { return geometry::sides() + alias::sides() + count() == 12 ? 0 : 1; }
