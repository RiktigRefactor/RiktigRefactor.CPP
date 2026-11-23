struct Order {
    int items = 2;
    static void add(Order &order, int count) { order.items += count; }
};

int main() {
    Order order;
    Order::add(order, 3);
    return order.items == 5 ? 0 : 1;
}
