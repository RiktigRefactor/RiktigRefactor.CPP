struct Order {
    int price = 3;
    int /*|*/total(int qty) const { return price * qty; }
};

int main() {
    Order order;
    int (Order::*compute)(int) const = &Order::total;
    return (order.*compute)(2) == 6 ? 0 : 1;
}
