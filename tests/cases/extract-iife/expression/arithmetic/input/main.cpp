int main() {
    double price = 2.5;
    int qty = 4;
    auto total = /*[*/price * qty/*]*/;
    return total == 10.0 ? 0 : 1;
}
