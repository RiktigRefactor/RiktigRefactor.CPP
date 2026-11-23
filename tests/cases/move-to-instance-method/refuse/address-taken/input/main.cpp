struct Order {
    int price = 2;
    static int /*|*/total(const Order &order) { return order.price; }
};

int main() {
    int (*compute)(const Order &) = &Order::total;
    return compute(Order()) == 2 ? 0 : 1;
}
