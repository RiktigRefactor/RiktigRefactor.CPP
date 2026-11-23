#pragma once

namespace shop {

class Counter {
  public:
    int count = 0;
    int step = 1;
    void bump(int times);
    int value() const { return count; }
};

} // namespace shop
