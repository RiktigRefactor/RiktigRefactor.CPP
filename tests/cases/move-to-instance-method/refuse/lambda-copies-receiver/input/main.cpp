struct Order {
    int price = 1;
    static int /*|*/snapshot(Order &order) {
        auto before = [=]() { return order.price; };
        order.price = 5;
        return before();
    }
};

int main() {
    Order order;
    return Order::snapshot(order) == 1 ? 0 : 1;
}
