struct Order {
    double price = 2.5;
    static double total(const Order &self, int qty) { return self.price * qty; }
};

int main() {
    Order order;
    Order *p = &order;
    double t = Order::total(order, 3);
    double u = Order::total(*p, 2);
    return t == 7.5 && u == 5.0 ? 0 : 1;
}
