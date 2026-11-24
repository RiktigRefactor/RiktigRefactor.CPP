struct Order {
    double price = 2.5;
    // begin-snippet: make-method-static-before
    double /*|*/total(int qty) const { return price * qty; }
    // end-snippet
};

int main() {
    Order order;
    Order *p = &order;
    double t = order.total(3);
    double u = p->total(2);
    return t == 7.5 && u == 5.0 ? 0 : 1;
}
