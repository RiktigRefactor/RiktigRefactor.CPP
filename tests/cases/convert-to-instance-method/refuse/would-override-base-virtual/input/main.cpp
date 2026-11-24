struct Base {
    virtual int kind() const { return 0; }
    virtual ~Base() = default;
};

struct Order : Base {
    static int /*|*/kind(const Order &order) { return 1; }
};

int main() {
    Order order;
    const Base &base = order;
    return base.kind() == 0 && Order::kind(order) == 1 ? 0 : 1;
}
