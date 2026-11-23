#include "order.h"
#include "pricing.h"

int main() { return shop::Pricing::code(5) == 15 ? 0 : 1; }
