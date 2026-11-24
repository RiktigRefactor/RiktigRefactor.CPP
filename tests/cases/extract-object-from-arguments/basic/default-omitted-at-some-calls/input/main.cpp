int sku() { return 7; }

// begin-snippet: extract-object-from-arguments-before
double /*|*/quote(int code, double price, int qty = 1) { return code + price * qty; }
// end-snippet

int main() {
    double first = quote(sku(), 2.5, 2);
    double second = quote(1, 4.0);
    return first == 12.0 && second == 5.0 ? 0 : 1;
}
