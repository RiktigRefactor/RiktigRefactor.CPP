struct Order {
    int base = 1;
    int /*|*/price(long factor) const { return base * 100; }
    static int price(const Order &order, double factor) { return order.base; }
};

int main() {
    Order order;
    return Order::price(order, 1L) == 1 && order.price(2L) == 100 ? 0 : 1;
}
