struct Order {
    int price = 3;
    int /*|*/total() const { return price; }
};

struct Gift {
    int total() const { return 0; }
};

template <typename T> int totalOf(const T &item) { return item.total(); }

int main() {
    Order order;
    Gift gift;
    return totalOf(order) == 3 && totalOf(gift) == 0 ? 0 : 1;
}
