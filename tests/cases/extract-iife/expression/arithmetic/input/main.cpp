int main() {
    double price = 2.5;
    int qty = 4;
    // begin-snippet: extract-iife-before
    auto total = /*[*/price * qty/*]*/;
    // end-snippet
    return total == 10.0 ? 0 : 1;
}
