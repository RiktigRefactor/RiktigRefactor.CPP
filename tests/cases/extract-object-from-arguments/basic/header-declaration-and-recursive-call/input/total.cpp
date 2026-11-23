#include "total.h"

int total(int base, int count, int step) {
    if (count == 0) {
        return base;
    }
    auto next = [&]() { return base + 2; };
    count -= 1;
    return total(next(), count, step);
}
