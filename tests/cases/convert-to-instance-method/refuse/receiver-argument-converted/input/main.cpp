struct Order {
    int price;
    Order(int value) : price(value) {}
    static int /*|*/total(const Order &order) { return order.price; }
};

int main() { return Order::total(5) == 5 ? 0 : 1; }
