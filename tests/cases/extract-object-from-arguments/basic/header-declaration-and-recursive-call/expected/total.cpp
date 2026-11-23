#include "total.h"

int total(int base, Steps steps) {
    if (steps.count == 0) {
        return base;
    }
    auto next = [&]() { return base + 2; };
    steps.count -= 1;
    return total(next(), Steps{.count = steps.count, .step = steps.step});
}
