struct Order;
int countOf(const Order &order);

struct Order {
    int items = 1;
    static void /*|*/add(int count, Order &order) { order.items += count + countOf(order); }
};

int countOf(const Order &order) { return order.items; }

int main() {
    Order order;
    Order::add(2, order);
    return order.items == 4 ? 0 : 1;
}
