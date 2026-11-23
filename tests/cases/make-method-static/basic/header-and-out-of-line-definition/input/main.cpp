#include "counter.h"

namespace shop {
int bumpTwice(Counter &counter) {
    counter.bump(1);
    counter.bump(1);
    return counter.value();
}
} // namespace shop

int main() {
    shop::Counter counter;
    counter.step = 3;
    counter.bump(2);
    return shop::bumpTwice(counter) == 12 ? 0 : 1;
}
