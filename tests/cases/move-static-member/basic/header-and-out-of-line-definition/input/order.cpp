#include "order.h"
#include "pricing.h"

namespace shop {

int Order::code(int base) { return base + offset; }

} // namespace shop
