struct Order {
    double price = 2.5;
    Order *next = nullptr;
    static double /*|*/total(const Order &order, int qty) {
        double own = order.price * qty;
        return order.next ? own + total(*order.next, qty) : own;
    }
    double twice() const { return total(*this, 2); }
};

int main() {
    Order last;
    Order first;
    first.next = &last;
    Order *p = &last;
    double t = Order::total(first, 2);
    double u = Order::total(*p, 2);
    return t == 10.0 && u == 5.0 && last.twice() == 5.0 ? 0 : 1;
}
