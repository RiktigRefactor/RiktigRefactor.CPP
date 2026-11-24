int main() {
    double price = 2.5;
    int qty = 4;
    // begin-snippet: extract-iife-after
    auto total = [](double price, int qty) { return price * qty; }(price, qty);
    // end-snippet
    return total == 10.0 ? 0 : 1;
}
