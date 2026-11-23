int trace[2];
int traced = 0;

struct Order {
    int items = 0;
    static void /*|*/add(int count, Order &order) { order.items += count; }
};

Order orders[1];

int next() {
    trace[traced++] = 1;
    return 1;
}

Order &pick() {
    trace[traced++] = 2;
    return orders[0];
}

int main() {
    Order::add(next(), pick());
    return trace[0] == 1 && trace[1] == 2 ? 0 : 1;
}
