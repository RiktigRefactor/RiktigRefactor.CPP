#include "shapes.h"

namespace alias = /*|*/shapes;

int count() {
    using namespace shapes;
    return sides();
}

int main() { return shapes::sides() + alias::sides() + count() == 12 ? 0 : 1; }
