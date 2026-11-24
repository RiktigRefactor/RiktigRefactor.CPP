struct Order {
    int price = 2;
};

int /*|*/total(const Order &order) { return order.price; }

int main() { return total(Order()) == 2 ? 0 : 1; }
