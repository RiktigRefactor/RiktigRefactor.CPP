#pragma once

namespace shop {

struct Order {
    static constexpr int offset = 10;
    static int /*|*/code(int base);
};

} // namespace shop
