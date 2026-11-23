struct Order {
    int count = 2;
};

struct Limits {
    static constexpr int minimum = 1;
  public:
    static constexpr int maximum = 5;
};

int main() {
    Order order;
    return order.count < Limits::maximum && Limits::maximum == 5 ? 0 : 1;
}
