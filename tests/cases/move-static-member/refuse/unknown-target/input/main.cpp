struct Order {
    static int /*|*/total() { return 1; }
};

int main() { return Order::total() == 1 ? 0 : 1; }
