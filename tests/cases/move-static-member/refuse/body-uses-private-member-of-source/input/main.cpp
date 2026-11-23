class Order {
    static constexpr int secret = 4;

  public:
    static int /*|*/reveal() { return secret; }
};

struct Pricing {};

int main() { return Order::reveal() == 4 ? 0 : 1; }
