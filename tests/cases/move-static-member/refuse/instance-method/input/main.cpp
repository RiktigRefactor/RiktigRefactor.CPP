struct Order {
    int /*|*/total() const { return 1; }
};

struct Pricing {};

int main() { return Order().total() == 1 ? 0 : 1; }
