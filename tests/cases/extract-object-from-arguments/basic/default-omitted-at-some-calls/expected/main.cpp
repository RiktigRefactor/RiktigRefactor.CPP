int sku() { return 7; }

struct QuoteLine {
    double price;
    int qty = 1;
};

// begin-snippet: extract-object-from-arguments-after
double quote(int code, QuoteLine line) { return code + line.price * line.qty; }
// end-snippet

int main() {
    double first = quote(sku(), QuoteLine{.price = 2.5, .qty = 2});
    double second = quote(1, QuoteLine{.price = 4.0});
    return first == 12.0 && second == 5.0 ? 0 : 1;
}
