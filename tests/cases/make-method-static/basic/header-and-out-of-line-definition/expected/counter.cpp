#include "counter.h"

namespace shop {

void Counter::bump(Counter &self, int times) {
    for (int i = 0; i < times; ++i) {
        self.count += self.step;
    }
}

} // namespace shop
