struct Order {
    double price = 2.5;
    Order *next = nullptr;
    // begin-snippet: convert-static-method-to-instance-method-after
    double total(int qty) const {
    // end-snippet
        double own = price * qty;
        return next ? own + next->total(qty) : own;
    }
    double twice() const { return this->total(2); }
};

int main() {
    Order last;
    Order first;
    first.next = &last;
    Order *p = &last;
    double t = first.total(2);
    double u = p->total(2);
    return t == 10.0 && u == 5.0 && last.twice() == 5.0 ? 0 : 1;
}
