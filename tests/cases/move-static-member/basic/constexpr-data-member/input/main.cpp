struct Order {
    static constexpr int /*|*/maximum = 5;
    int count = 2;
};

struct Limits {
    static constexpr int minimum = 1;
};

int main() {
    Order order;
    return order.count < Order::maximum && order.maximum == 5 ? 0 : 1;
}
