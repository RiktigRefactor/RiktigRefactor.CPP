#pragma once

namespace shop {

class Counter {
  public:
    int count = 0;
    int step = 1;
    static void bump(Counter &self, int times);
    int value() const { return count; }
};

} // namespace shop
