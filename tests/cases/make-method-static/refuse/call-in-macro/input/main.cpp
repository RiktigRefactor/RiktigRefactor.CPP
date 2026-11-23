struct Order {
    int price = 2;
    int /*|*/total() const { return price; }
};

#define TOTAL(order) (order).total()

int main() {
    Order order;
    return TOTAL(order) == 2 ? 0 : 1;
}
