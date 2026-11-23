#include "counter.h"

namespace shop {
int bumpTwice(Counter &counter) {
    Counter::bump(counter, 1);
    Counter::bump(counter, 1);
    return counter.value();
}
} // namespace shop

int main() {
    shop::Counter counter;
    counter.step = 3;
    shop::Counter::bump(counter, 2);
    return shop::bumpTwice(counter) == 12 ? 0 : 1;
}
