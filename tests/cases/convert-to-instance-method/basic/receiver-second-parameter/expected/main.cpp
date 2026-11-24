struct Order;
int countOf(const Order &order);

struct Order {
    int items = 1;
    void add(int count) { items += count + countOf(*this); }
};

int countOf(const Order &order) { return order.items; }

int main() {
    Order order;
    order.add(2);
    return order.items == 4 ? 0 : 1;
}
