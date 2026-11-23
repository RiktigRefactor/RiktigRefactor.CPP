int main() {
    double price = 2.5;
    int qty = 4;
    auto total = [](double price, int qty) { return price * qty; }(price, qty);
    return total == 10.0 ? 0 : 1;
}
