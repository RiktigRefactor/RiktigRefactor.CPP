int trace[2];
int traced = 0;

struct Order {
    int price = 2;
    int /*|*/total(int qty) const { return price * qty; }
};

Order orders[1];

Order &pick() {
    trace[traced++] = 1;
    return orders[0];
}

int quantity() {
    trace[traced++] = 2;
    return 3;
}

int main() {
    int t = pick().total(quantity());
    return t == 6 && trace[0] == 1 && trace[1] == 2 ? 0 : 1;
}
