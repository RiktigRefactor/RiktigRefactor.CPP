#pragma once

namespace shop {

struct Pricing {
    static constexpr int offset = 1000;
  public:
    static int code(int base);
};

} // namespace shop
