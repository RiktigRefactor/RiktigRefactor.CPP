#include "counter.h"

namespace shop {

void Counter::/*|*/bump(int times) {
    for (int i = 0; i < times; ++i) {
        count += step;
    }
}

} // namespace shop
