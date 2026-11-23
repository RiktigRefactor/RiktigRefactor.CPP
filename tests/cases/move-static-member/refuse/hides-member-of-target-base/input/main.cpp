struct Base {
    static int rate() { return 1; }
};

struct Pricing : Base {};

struct Order {
    static int /*|*/rate() { return 2; }
};

int main() { return Pricing::rate() == 1 && Order::rate() == 2 ? 0 : 1; }
