struct Pricing {
    static int scale(long value) { return 100; }
};

struct Order {
    static int /*|*/scale(int value) { return 1; }
};

int main() { return Pricing::scale(1) == 100 && Order::scale(1) == 1 ? 0 : 1; }
