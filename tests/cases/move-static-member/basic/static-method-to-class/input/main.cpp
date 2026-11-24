struct Order {
    static constexpr double rate = 0.5;
    int quantity = 3;

    // begin-snippet: move-static-member-before
    static double /*|*/total(const Order &self, int qty) { return self.quantity * qty * rate; }
    // end-snippet
    double doubled() const;
};

struct Pricing {
    static constexpr double rate = 2.0;
};

double Order::doubled() const { return total(*this, 2) * 2; }

int main() {
    Order order;
    double t = Order::total(order, 4);
    return t == 6.0 && order.doubled() == 6.0 ? 0 : 1;
}
