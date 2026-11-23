struct Order {
    int price = 2;
    static int /*|*/total(const Order *order) { return order ? order->price : 0; }
};

int main() { return Order::total(nullptr) == 0 ? 0 : 1; }
