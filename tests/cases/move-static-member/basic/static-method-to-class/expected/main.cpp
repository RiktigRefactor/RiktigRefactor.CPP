struct Order {
    static constexpr double rate = 0.5;
    int quantity = 3;

    double doubled() const;
};

struct Pricing {
    static constexpr double rate = 2.0;
  public:
    static double total(const Order &self, int qty) { return self.quantity * qty * Order::rate; }
};

double Order::doubled() const { return Pricing::total(*this, 2) * 2; }

int main() {
    Order order;
    double t = Pricing::total(order, 4);
    return t == 6.0 && order.doubled() == 6.0 ? 0 : 1;
}
