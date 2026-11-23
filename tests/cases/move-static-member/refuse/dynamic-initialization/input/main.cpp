int initialized = 0;
int nextValue() { return ++initialized; }

struct Order {
    static inline int /*|*/first = nextValue();
};

struct Pricing {};

int main() { return Order::first == 1 ? 0 : 1; }
