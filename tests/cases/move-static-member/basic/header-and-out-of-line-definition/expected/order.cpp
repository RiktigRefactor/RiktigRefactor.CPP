#include "order.h"
#include "pricing.h"

namespace shop {

int Pricing::code(int base) { return base + Order::offset; }

} // namespace shop
