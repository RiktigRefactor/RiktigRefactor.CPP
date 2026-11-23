struct Order {
    int items = 2;
    void /*|*/add(int count) { items += count; }
};

int main() {
    Order order;
    order.add(3);
    return order.items == 5 ? 0 : 1;
}
